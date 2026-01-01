/**
 * WebSocket client for Audio Share protocol
 */

export interface AudioFormat {
    sampleRate: number;
    channels: number;
    encoding: string;  // 'f32', 's16', 's24', 's32', 's8'
    bitsPerSample: number;
}

interface ProtocolMessage {
    type: 'format' | 'heartbeat' | 'error';
    [key: string]: unknown;
}

export class WebSocketClient {
    private ws: WebSocket | null = null;
    private url: string;
    private heartbeatInterval: number | null = null;

    // Event handlers
    public onFormat: ((format: AudioFormat) => void) | null = null;
    public onAudioData: ((data: ArrayBuffer) => void) | null = null;
    public onError: ((error: Error) => void) | null = null;
    public onClose: (() => void) | null = null;

    constructor(url: string) {
        this.url = url;
    }

    public connect(): Promise<void> {
        return new Promise((resolve, reject) => {
            try {
                this.ws = new WebSocket(this.url);
                this.ws.binaryType = 'arraybuffer';

                this.ws.onopen = () => {
                    console.log('WebSocket connected');
                    this.startHeartbeat();
                    resolve();
                };

                this.ws.onmessage = (event) => {
                    this.handleMessage(event.data);
                };

                this.ws.onerror = (event) => {
                    console.error('WebSocket error:', event);
                    reject(new Error('WebSocket connection failed'));
                };

                this.ws.onclose = (event) => {
                    console.log('WebSocket closed:', event.code, event.reason);
                    this.stopHeartbeat();
                    
                    if (this.onClose) {
                        this.onClose();
                    }
                };

            } catch (error) {
                reject(error);
            }
        });
    }

    public disconnect(): void {
        this.stopHeartbeat();
        
        if (this.ws) {
            this.ws.close(1000, 'Client disconnect');
            this.ws = null;
        }
    }

    private handleMessage(data: string | ArrayBuffer): void {
        if (typeof data === 'string') {
            // Handle pong response from server
            if (data === 'pong') {
                // Heartbeat acknowledged
                return;
            }
            
            // JSON message (format, control commands)
            try {
                const message = JSON.parse(data) as ProtocolMessage;
                this.handleControlMessage(message);
            } catch (error) {
                console.error('Failed to parse message:', error);
            }
        } else {
            // Binary message (audio data)
            if (this.onAudioData) {
                this.onAudioData(data);
            }
        }
    }

    private handleControlMessage(message: ProtocolMessage): void {
        switch (message.type) {
            case 'format':
                if (this.onFormat) {
                    const format: AudioFormat = {
                        sampleRate: message.sampleRate as number,
                        channels: message.channels as number,
                        encoding: message.encoding as string,
                        bitsPerSample: message.bitsPerSample as number ?? this.getBitsPerSample(message.encoding as string),
                    };
                    this.onFormat(format);
                }
                break;

            case 'heartbeat':
                // Respond to heartbeat
                this.sendHeartbeat();
                break;

            case 'error':
                if (this.onError) {
                    this.onError(new Error(message.message as string));
                }
                break;

            default:
                console.warn('Unknown message type:', message.type);
        }
    }

    private getBitsPerSample(encoding: string): number {
        const bits: Record<string, number> = {
            'f32': 32,
            's32': 32,
            's24': 24,
            's16': 16,
            's8': 8,
        };
        return bits[encoding] || 16;
    }

    private startHeartbeat(): void {
        this.heartbeatInterval = window.setInterval(() => {
            this.sendHeartbeat();
        }, 3000);
    }

    private stopHeartbeat(): void {
        if (this.heartbeatInterval !== null) {
            clearInterval(this.heartbeatInterval);
            this.heartbeatInterval = null;
        }
    }

    private sendHeartbeat(): void {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            // Server expects simple "ping" string
            this.ws.send('ping');
        }
    }

    public isConnected(): boolean {
        return this.ws !== null && this.ws.readyState === WebSocket.OPEN;
    }
}
