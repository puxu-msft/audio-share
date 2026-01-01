/**
 * Audio visualizer using Canvas API
 * 
 * Renders frequency bars based on audio analyser data.
 */

export class Visualizer {
    private canvas: HTMLCanvasElement;
    private ctx: CanvasRenderingContext2D;
    private analyser: AnalyserNode | null = null;
    
    private animationId: number | null = null;
    private dataArray: Uint8Array<ArrayBuffer> | null = null;
    
    // Colors
    private readonly BAR_COLOR_START = '#4CAF50';
    private readonly BAR_COLOR_END = '#8BC34A';
    private readonly BACKGROUND_COLOR = '#1a1a2e';

    constructor(canvasId: string) {
        this.canvas = document.getElementById(canvasId) as HTMLCanvasElement;
        this.ctx = this.canvas.getContext('2d')!;
        
        // Handle resize
        this.resize();
        window.addEventListener('resize', () => this.resize());
    }

    private resize(): void {
        const parent = this.canvas.parentElement;
        if (parent) {
            this.canvas.width = parent.clientWidth;
            this.canvas.height = parent.clientHeight || 150;
        }
    }

    public setAnalyser(analyser: AnalyserNode): void {
        this.analyser = analyser;
        this.dataArray = new Uint8Array(analyser.frequencyBinCount);
    }

    public start(): void {
        if (!this.analyser || !this.dataArray) return;
        
        const draw = () => {
            if (!this.analyser || !this.dataArray) return;
            
            this.animationId = requestAnimationFrame(draw);
            
            // Get frequency data
            this.analyser.getByteFrequencyData(this.dataArray);
            
            // Clear canvas
            this.ctx.fillStyle = this.BACKGROUND_COLOR;
            this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
            
            // Calculate bar dimensions
            const bufferLength = this.analyser.frequencyBinCount;
            const barWidth = (this.canvas.width / bufferLength) * 2.5;
            let x = 0;
            
            // Draw bars
            for (let i = 0; i < bufferLength; i++) {
                const value = this.dataArray[i];
                const percent = value / 255;
                const barHeight = percent * this.canvas.height;
                
                // Create gradient for bar
                const gradient = this.ctx.createLinearGradient(
                    x, this.canvas.height - barHeight,
                    x, this.canvas.height
                );
                gradient.addColorStop(0, this.BAR_COLOR_END);
                gradient.addColorStop(1, this.BAR_COLOR_START);
                
                this.ctx.fillStyle = gradient;
                this.ctx.fillRect(
                    x,
                    this.canvas.height - barHeight,
                    barWidth - 1,
                    barHeight
                );
                
                x += barWidth;
                if (x > this.canvas.width) break;
            }
        };
        
        draw();
    }

    public stop(): void {
        if (this.animationId !== null) {
            cancelAnimationFrame(this.animationId);
            this.animationId = null;
        }
        
        // Clear canvas
        this.ctx.fillStyle = this.BACKGROUND_COLOR;
        this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
        
        // Draw idle state
        this.drawIdleState();
    }

    private drawIdleState(): void {
        this.ctx.fillStyle = '#666';
        this.ctx.font = '14px sans-serif';
        this.ctx.textAlign = 'center';
        this.ctx.textBaseline = 'middle';
        this.ctx.fillText(
            'Not Connected',
            this.canvas.width / 2,
            this.canvas.height / 2
        );
    }

    public destroy(): void {
        this.stop();
        window.removeEventListener('resize', () => this.resize());
    }
}
