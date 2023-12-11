(function () {
  'use strict';

  var importObject = {
    imports: {
      imported_func: function(arg) {
        console.log(arg);
      }
    }
  };

  const PlayerState = {
    Destroyed: "destroyed",
    Error: "error",
    Frozen: "frozen",
    Loading: "loading",
    Paused: "paused",
    Playing: "playing",
    Stopped: "stopped",
  };

  const PlayMode = {
    Bounce: "bounce",
    Normal: "normal",
  };

  let requestedLoading = false;
  let _tvg;
  let _canvas;

  let totalFrame = 0;
  let currentState = PlayerState.Stopped;
  let currentFrame = 0;
  let beginTime = Date.now();
  let imageData;
  let speed = 1;
  let counter = 1;
  let count = 0;
  let mode = PlayMode.Normal;
  let loop = true;
  let direction = 1;
  let duration;

  // console.log("worker: " + e.data.type);

  const _animLoop = () => {
    if (!_tvg || !_canvas) {
      return;
    }

    if (_libUpdate()) {
      _libRender();
      requestAnimationFrame(_animLoop);
    }
  };

  const _libUpdate = () => {
    if (currentState !== PlayerState.Playing) {
      return false;
    }

    const duration = _duration();
    const currentTime = Date.now() / 1000;
    currentFrame = (currentTime - beginTime) / duration * totalFrame * speed;

    if (direction === -1) {
      currentFrame = totalFrame - currentFrame;
    }

    if (
      (direction === 1 && currentFrame >= totalFrame) ||
      (direction === -1 && currentFrame <= 0)
    ) {
      const totalCount = count ? mode === PlayMode.Bounce ? count * 2 : count : 0;
      if (loop || (totalCount && _counter < totalCount)) {
        if (mode === PlayMode.Bounce) {
          direction = direction === 1 ? -1 : 1;
          currentFrame = direction === 1 ? 0 : totalFrame;
        }

        if (count) {
          _counter += 1;
        }

        // await _wait(this.intermission);
        _libPlay();
        return true;
      }

      // this.dispatchEvent(new CustomEvent(PlayerEvent.Complete));
      currentState = PlayerState.Stopped;
    }

    // this.dispatchEvent(new CustomEvent(PlayerEvent.Frame, {
    //   detail: {
    //     frame: currentFrame,
    //   },
    // }));
    return _frame(currentFrame);
  };

  const _libRender = () => {
    _resize(_canvas.width, _canvas.height);
    const isUpdated = _update();
    
    if (!isUpdated) {
      return;
    }

    // render
    const buffer = _render();
    _flush(buffer);
  };

  const _libPlay = () => {
    totalFrame = _totalFrame();
    if (totalFrame < 1) {
      return;
    }

    beginTime = Date.now() / 1000;
    if (currentState == PlayerState.Playing) {
      return;
    }

    currentState = PlayerState.Playing;
    requestAnimationFrame(_animLoop);
  };

  const _render = () => {
    const buffer = _tvg.render();
    return buffer;
  };

  const _totalFrame = () => {
    const totalFrame = _tvg.totalFrame();
    return totalFrame;
  };

  const _duration = () => {
    if (duration) {
      return duration;
    }

    duration = _tvg.duration();
    return duration;
  };

  const _frame = (no) => {
    const isUpdated = _tvg.frame(no);
    return isUpdated;
  };

  const _flush = (buffer) => {
    // const clampedBuffer = new Uint8ClampedArray(buffer);
    // if (clampedBuffer.length < 1) {
    //   return;
    // }

    const ctx = _canvas.getContext('2d');
    const imageData = ctx.createImageData(_canvas.width, _canvas.height);
    imageData.data.set(new Uint8ClampedArray(buffer));


    ctx.putImageData(imageData, 0, 0);
    // ctx.putImageData(new ImageData(), _canvas.width, _canvas.height), 0, 0, 0, 0, _canvas.width, _canvas.height);
  };

  const _resize = (width, height) => {
    if (_canvas.width === width && _canvas.height === height) { return; }
    _tvg.resize(width, height);
  };

  const _update = () => {
    const isUpdated = _tvg.update();
    return isUpdated;
  };

  const _load = (data, rPath, width, height) => {
    const isLoaded = _tvg.load(
      data, 'lottie', width, height, rPath,
    );
    return isLoaded;
  };

  self.onmessage = (e) => {
    switch (e.data.type) {
      case "loadWasm":
        if (!requestedLoading) {
          requestedLoading = true;
    
          importScripts('/web/dist/thorvg-wasm.js');
          Module().then((module) => {
              _tvg = new module.TvgLottieAnimation();
              self.postMessage({
                type: "loadedWasm",
                tvg: _tvg,
              });
          });
        }
        break;
      case "loadCanvas":
        const { offscreen } = e.data;
        _canvas = offscreen;
        break;
      case "load":
        const { data, rPath, width, height } = e.data;

        self.postMessage({
          type: "loaded",
          isLoaded: _load(data, rPath, width, height),
        });
        break;
      case "resize":
        _resize(e.data.width, e.data.height);
        self.postMessage({
          type: "resized",
        });
        break;
      case "update":
        self.postMessage({
          type: "updated",
          isUpdated: _update(),
        });
        break;
      case "render":
        self.postMessage({
          type: "rendered",
          buffer: _render(),
        });
        break;
      case "totalFrame":
        self.postMessage({
          type: "totalFrame",
          totalFrame: _totalFrame(),
        });
        
        break;
      case "duration":
        self.postMessage({
          type: "duration",
          totalFrame: _duration(),
        });
        
        break;
      case "frame":
        self.postMessage({
          type: "frame",
          isUpdated: _frame(e.data.frame),
        });
        
        break;
      case "libLoad":
        _load(e.data.data, e.data.rPath, e.data.width, e.data.height);
        _libRender();
        _libPlay();
      break;
      case "libRender":
        _libRender();

        break;
      case "libPlay":
        _libPlay();

        break;
    }
  };

})();
