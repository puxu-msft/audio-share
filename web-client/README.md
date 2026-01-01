# Audio Share Web Client

Web browser client for Audio Share, allowing you to receive and play audio from the server directly in your browser.

## Architecture

```
┌─────────────────┐     WebSocket      ┌─────────────────────┐
│   Web Browser   │◄──────────────────►│  WebSocket Server   │
│                 │                    │  (server-core)      │
│  ┌───────────┐  │                    │                     │
│  │Web Audio  │  │   Audio Data       │  ┌───────────────┐  │
│  │   API     │◄─┼────(binary)────────┼──│ Audio Capture │  │
│  └───────────┘  │                    │  └───────────────┘  │
└─────────────────┘                    └─────────────────────┘
```

## Protocol

### WebSocket Connection
- Connect to `ws://server:port/audio`
- Server sends audio format as first message (JSON)
- Server streams PCM audio data as binary messages

### Message Types

1. **Audio Format** (JSON, server → client)
```json
{
  "type": "format",
  "sampleRate": 48000,
  "channels": 2,
  "encoding": "f32"
}
```

2. **Audio Data** (Binary, server → client)
- Raw PCM samples in the format specified above

3. **Control Commands** (JSON, bidirectional)
```json
{
  "type": "heartbeat"
}
```

## Browser Requirements

- Modern browser with Web Audio API support
- Chrome 35+, Firefox 25+, Safari 6+, Edge 12+

## Development

```bash
cd web-client
npm install
npm run dev
```

## Building

```bash
npm run build
```

Output will be in `dist/` directory, can be served as static files.
