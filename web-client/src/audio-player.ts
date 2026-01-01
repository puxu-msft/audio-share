/**
 * Audio player using Web Audio API
 * 
 * Receives PCM audio data and plays it through the browser's audio system.
 */

import { AudioFormat } from './websocket-client';

export class AudioPlayer {
    private audioContext: AudioContext;
    private gainNode: GainNode;
    public analyser: AnalyserNode;
    
    private format: AudioFormat;
    private sampleRate: number;
    private channels: number;
    private bytesPerSample: number;

    // Audio buffering
    private audioQueue: Float32Array[] = [];
    private isPlaying = false;
    private nextPlayTime = 0;
    
    // Buffer settings
    private readonly MIN_BUFFER_TIME = 0.05;  // 50ms minimum buffer
    private readonly MAX_BUFFER_TIME = 0.3;   // 300ms maximum buffer

    constructor(format: AudioFormat) {
        this.format = format;
        this.sampleRate = format.sampleRate;
        this.channels = format.channels;
        this.bytesPerSample = format.bitsPerSample / 8;

        // Create audio context
        this.audioContext = new AudioContext({ sampleRate: this.sampleRate });

        // Create gain node for volume control
        this.gainNode = this.audioContext.createGain();
        this.gainNode.connect(this.audioContext.destination);

        // Create analyser for visualization
        this.analyser = this.audioContext.createAnalyser();
        this.analyser.fftSize = 256;
        this.gainNode.connect(this.analyser);
    }

    public start(): void {
        if (this.audioContext.state === 'suspended') {
            this.audioContext.resume();
        }
        this.isPlaying = true;
        this.nextPlayTime = this.audioContext.currentTime + this.MIN_BUFFER_TIME;
        this.scheduleBuffers();
    }

    public stop(): void {
        this.isPlaying = false;
        this.audioQueue = [];
        this.audioContext.close();
    }

    public setVolume(volume: number): void {
        this.gainNode.gain.value = Math.max(0, Math.min(1, volume));
    }

    public pushAudioData(data: ArrayBuffer): void {
        // Convert incoming PCM data to Float32Array
        const floatData = this.convertToFloat32(data);
        
        // Add to queue
        this.audioQueue.push(floatData);

        // Prevent buffer overflow
        const totalBuffered = this.audioQueue.reduce((acc, buf) => acc + buf.length, 0);
        const bufferedTime = totalBuffered / (this.sampleRate * this.channels);
        
        if (bufferedTime > this.MAX_BUFFER_TIME) {
            // Drop oldest buffers
            while (this.audioQueue.length > 1) {
                const oldest = this.audioQueue[0];
                const oldestTime = oldest.length / (this.sampleRate * this.channels);
                if (bufferedTime - oldestTime > this.MIN_BUFFER_TIME) {
                    this.audioQueue.shift();
                } else {
                    break;
                }
            }
        }

        // Schedule playback
        if (this.isPlaying) {
            this.scheduleBuffers();
        }
    }

    private scheduleBuffers(): void {
        const currentTime = this.audioContext.currentTime;

        // Reset timing if we've fallen behind
        if (this.nextPlayTime < currentTime) {
            this.nextPlayTime = currentTime + this.MIN_BUFFER_TIME;
        }

        // Schedule available buffers
        while (this.audioQueue.length > 0) {
            const floatData = this.audioQueue.shift()!;
            
            // Create audio buffer
            const numSamples = floatData.length / this.channels;
            const audioBuffer = this.audioContext.createBuffer(
                this.channels,
                numSamples,
                this.sampleRate
            );

            // Deinterleave channels
            for (let channel = 0; channel < this.channels; channel++) {
                const channelData = audioBuffer.getChannelData(channel);
                for (let i = 0; i < numSamples; i++) {
                    channelData[i] = floatData[i * this.channels + channel];
                }
            }

            // Create buffer source and schedule playback
            const source = this.audioContext.createBufferSource();
            source.buffer = audioBuffer;
            source.connect(this.gainNode);
            source.start(this.nextPlayTime);

            // Update next play time
            this.nextPlayTime += audioBuffer.duration;
        }
    }

    private convertToFloat32(data: ArrayBuffer): Float32Array {
        const dataView = new DataView(data);
        const numSamples = data.byteLength / this.bytesPerSample;
        const floatData = new Float32Array(numSamples);

        switch (this.format.encoding) {
            case 'f32':
                // Already float32, just copy
                for (let i = 0; i < numSamples; i++) {
                    floatData[i] = dataView.getFloat32(i * 4, true);
                }
                break;

            case 's32':
                // 32-bit signed integer
                for (let i = 0; i < numSamples; i++) {
                    floatData[i] = dataView.getInt32(i * 4, true) / 2147483648;
                }
                break;

            case 's24':
                // 24-bit signed integer (packed)
                for (let i = 0; i < numSamples; i++) {
                    const offset = i * 3;
                    let value = dataView.getUint8(offset) |
                               (dataView.getUint8(offset + 1) << 8) |
                               (dataView.getUint8(offset + 2) << 16);
                    // Sign extend
                    if (value & 0x800000) {
                        value |= 0xFF000000;
                    }
                    floatData[i] = value / 8388608;
                }
                break;

            case 's16':
                // 16-bit signed integer
                for (let i = 0; i < numSamples; i++) {
                    floatData[i] = dataView.getInt16(i * 2, true) / 32768;
                }
                break;

            case 's8':
                // 8-bit signed integer
                for (let i = 0; i < numSamples; i++) {
                    floatData[i] = dataView.getInt8(i) / 128;
                }
                break;

            default:
                console.warn('Unknown encoding:', this.format.encoding);
                // Assume 16-bit
                for (let i = 0; i < numSamples; i++) {
                    floatData[i] = dataView.getInt16(i * 2, true) / 32768;
                }
        }

        return floatData;
    }

    public getBufferLatency(): number {
        const totalBuffered = this.audioQueue.reduce((acc, buf) => acc + buf.length, 0);
        const bufferedTime = totalBuffered / (this.sampleRate * this.channels);
        const scheduledAhead = Math.max(0, this.nextPlayTime - this.audioContext.currentTime);
        return (bufferedTime + scheduledAhead) * 1000;
    }
}
