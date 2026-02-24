#!/usr/bin/env python3
"""Dev server for the WASM build with proper MIME types and headers."""
import http.server
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
DIRECTORY = "build-web"


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "application/javascript",
        ".data": "application/octet-stream",
    }

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def end_headers(self):
        # Required for SharedArrayBuffer (some SDL/Emscripten features)
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # Disable caching during development
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        super().end_headers()


print(f"Serving {DIRECTORY}/ at http://localhost:{PORT}")
http.server.HTTPServer(("", PORT), Handler).serve_forever()
