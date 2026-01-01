/**
 * Audio Share Web Client
 * 
 * Connects to Audio Share server via WebSocket and plays audio using Web Audio API.
 */

import { AudioPlayer } from './audio-player';
import { WebSocketClient, AudioFormat } from './websocket-client';
import { Visualizer } from './visualizer';

class App {
    private wsClient: WebSocketClient | null = null;
    private audioPlayer: AudioPlayer | null = null;
    private visualizer: Visualizer | null = null;

    // DOM elements
    private serverAddressInput: HTMLInputElement;
    private connectBtn: HTMLButtonElement;
    private disconnectBtn: HTMLButtonElement;
    private statusEl: HTMLElement;
    private formatEl: HTMLElement;
    private latencyEl: HTMLElement;
    private volumeInput: HTMLInputElement;
    private volumeValueEl: HTMLElement;

    constructor() {
        // Get DOM elements
        this.serverAddressInput = document.getElementById('server-address') as HTMLInputElement;
        this.connectBtn = document.getElementById('connect-btn') as HTMLButtonElement;
        this.disconnectBtn = document.getElementById('disconnect-btn') as HTMLButtonElement;
        this.statusEl = document.getElementById('status') as HTMLElement;
        this.formatEl = document.getElementById('format') as HTMLElement;
        this.latencyEl = document.getElementById('latency') as HTMLElement;
        this.volumeInput = document.getElementById('volume') as HTMLInputElement;
        this.volumeValueEl = document.getElementById('volume-value') as HTMLElement;

        // Initialize visualizer
        this.visualizer = new Visualizer('visualizer-canvas');

        // Load saved server address
        const savedAddress = localStorage.getItem('audio-share-server');
        if (savedAddress) {
            this.serverAddressInput.value = savedAddress;
        }

        // Bind event handlers
        this.connectBtn.addEventListener('click', () => this.connect());
        this.disconnectBtn.addEventListener('click', () => this.disconnect());
        this.volumeInput.addEventListener('input', () => this.updateVolume());

        // Handle Enter key in server address input
        this.serverAddressInput.addEventListener('keypress', (e) => {
            if (e.key === 'Enter') {
                this.connect();
            }
        });
    }

    private async connect() {
        const address = this.serverAddressInput.value.trim();
        if (!address) {
            alert('Please enter server address');
            return;
        }

        // Save address
        localStorage.setItem('audio-share-server', address);

        // Update UI
        this.setStatus('connecting');
        this.connectBtn.disabled = true;

        try {
            // Parse address
            const [host, portStr] = address.includes(':') 
                ? address.split(':') 
                : [address, '65530'];
            const port = parseInt(portStr, 10);

            // Create WebSocket client
            const wsUrl = `ws://${host}:${port}/audio`;
            this.wsClient = new WebSocketClient(wsUrl);

            // Set up event handlers
            this.wsClient.onFormat = (format) => this.handleAudioFormat(format);
            this.wsClient.onAudioData = (data) => this.handleAudioData(data);
            this.wsClient.onError = (error) => this.handleError(error);
            this.wsClient.onClose = () => this.handleClose();

            // Connect
            await this.wsClient.connect();

            // Update UI
            this.setStatus('connected');
            this.connectBtn.disabled = true;
            this.disconnectBtn.disabled = false;
            this.serverAddressInput.disabled = true;

        } catch (error) {
            console.error('Connection failed:', error);
            this.handleError(error as Error);
        }
    }

    private disconnect() {
        if (this.wsClient) {
            this.wsClient.disconnect();
            this.wsClient = null;
        }

        if (this.audioPlayer) {
            this.audioPlayer.stop();
            this.audioPlayer = null;
        }

        this.handleClose();
    }

    private handleAudioFormat(format: AudioFormat) {
        console.log('Received audio format:', format);

        // Update format display
        const encodingName = this.getEncodingName(format.encoding);
        this.formatEl.textContent = `${format.sampleRate}Hz, ${format.channels}ch, ${encodingName}`;

        // Create audio player
        this.audioPlayer = new AudioPlayer(format);
        this.audioPlayer.setVolume(parseInt(this.volumeInput.value, 10) / 100);

        // Connect visualizer
        if (this.visualizer && this.audioPlayer.analyser) {
            this.visualizer.setAnalyser(this.audioPlayer.analyser);
            this.visualizer.start();
        }

        this.audioPlayer.start();
    }

    private handleAudioData(data: ArrayBuffer) {
        if (this.audioPlayer) {
            this.audioPlayer.pushAudioData(data);

            // Update latency display
            const latency = this.audioPlayer.getBufferLatency();
            this.latencyEl.textContent = `${Math.round(latency)}ms`;
        }
    }

    private handleError(error: Error) {
        console.error('WebSocket error:', error);
        this.setStatus('disconnected');
        this.resetUI();
        alert(`Connection error: ${error.message}`);
    }

    private handleClose() {
        console.log('Connection closed');
        this.setStatus('disconnected');
        this.resetUI();

        if (this.visualizer) {
            this.visualizer.stop();
        }
    }

    private resetUI() {
        this.connectBtn.disabled = false;
        this.disconnectBtn.disabled = true;
        this.serverAddressInput.disabled = false;
        this.formatEl.textContent = '-';
        this.latencyEl.textContent = '-';
    }

    private setStatus(status: 'connecting' | 'connected' | 'disconnected') {
        this.statusEl.className = `value ${status}`;
        this.statusEl.textContent = status.charAt(0).toUpperCase() + status.slice(1);
    }

    private updateVolume() {
        const volume = parseInt(this.volumeInput.value, 10);
        this.volumeValueEl.textContent = `${volume}%`;

        if (this.audioPlayer) {
            this.audioPlayer.setVolume(volume / 100);
        }
    }

    private getEncodingName(encoding: string): string {
        const names: Record<string, string> = {
            'f32': 'Float32',
            's8': 'Int8',
            's16': 'Int16',
            's24': 'Int24',
            's32': 'Int32',
        };
        return names[encoding] || encoding;
    }
}

// Initialize app when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    new App();
});
