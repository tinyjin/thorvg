import { createServer } from "http";
import fs from "fs";

createServer(function (req, response) {
  if (req.url.endsWith("lottie-player.js")) {
    fs.readFile("../dist/lottie-player.js", function (err, html) {
      if (err) {
        throw err;
      }
      response.writeHead(200, {
        "Content-Type": "text/javascript; charset=utf8",
        "Cross-Origin-Opener-Policy": "same-origin",
        "Cross-Origin-Embedder-Policy": "require-corp",
      });
      response.write(html);
      response.end();
    });
    return;
  }
  if (req.url.endsWith("stats.min.js")) {
    fs.readFile("./benchmark.js", function (err, html) {
      if (err) {
        throw err;
      }
      response.writeHead(200, {
        "Content-Type": "text/javascript; charset=utf8",
        "Cross-Origin-Opener-Policy": "same-origin",
        "Cross-Origin-Embedder-Policy": "require-corp",
      });
      response.write(html);
      response.end();
    });
    return;
  }
  if (req.url.endsWith('thorvg-wasm.js')) {
    fs.readFile('../dist/thorvg-wasm.js', function (err, html) {
      if (err) {
          throw err;
      }
      response.writeHead(200, {
        'Content-Type': 'text/javascript; charset=utf8',
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
      })
      response.write(html);
      response.end();
    });
    return;
  }
  if (req.url.endsWith('thorvg-wasm.worker.js')) {
    fs.readFile('../dist/thorvg-wasm.worker.js', function (err, html) {
      if (err) {
          throw err;
      }
      response.writeHead(200, {
        'Content-Type': 'text/javascript; charset=utf8',
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
      })
      response.write(html);
      response.end();
    });
    return;
  }

  fs.readFile("./benchmark.html", function (err, html) {
    if (err) {
      throw err;
    }

    response.writeHead(200, {
      "Content-Type": "text/html; charset=utf8",
      "Cross-Origin-Opener-Policy": "same-origin",
      "Cross-Origin-Embedder-Policy": "require-corp",
    });
    response.write(html);
    response.end();
  });
}).listen(8000);
