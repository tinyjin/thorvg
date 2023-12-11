import { html, PropertyValueMap, type TemplateResult } from 'lit';
import { customElement } from 'lit/decorators.js';


// @ts-ignore: WASM Glue code doesn't have type & Only available on build progress
// import Module from '../dist/thorvg-wasm';
import { ExportableType, LibraryVersion, LottiePlayerModel, PlayerEvent, PlayerState, PlayMode } from './lottie-player.model';
import { THORVG_VERSION } from './version';

// let _wasmModule: any;
let _tvg: any;
let _module: any;
// (async () => {  
//   _module = await Module();
//   _tvg = new _module.TvgLottieAnimation();

//   // @ts-ignore
//   window.tvg = _tvg;
// })();

/**
 * Parse a resource into a bytes
 */
const _parseSrc = async (src: string | object): Promise<Uint8Array> => {
  let data = src;
  if (typeof data === "object") {
    data = JSON.stringify(data);
  } else if (typeof data === "string") {
    try {
      data = JSON.parse(data);
    } catch (err) {
      const json = await _fromURL(data as string);
      data = JSON.stringify(json);
    }
  }

  const encoder = new TextEncoder();
  return encoder.encode(data as string);
}

const _fromURL = async (url: string): Promise<Record<string, any>> => {
  if (typeof url !== "string") {
    throw new Error(`The url value must be a string`);
  }

  let json;

  try {
    // Try construct an absolute URL from the src URL
    const srcUrl: URL = new URL(url);

    // Fetch the JSON file from the URL
    const result: any = await fetch(srcUrl.toString());

    json = await result.json();
  } catch (err) {
    throw new Error(
      `An error occurred while trying to load the Lottie file from URL`
    );
  }

  return json;
}

const _wait = (timeToDelay: number) => {
  return new Promise((resolve) => setTimeout(resolve, timeToDelay))
};

// const _observerCallback = (entries: IntersectionObserverEntry[]) => {
//   const entry = entries[0];
//   const target = entry.target as LottiePlayer;
  
//   if (entry.isIntersecting) {
//     if (target.currentState === PlayerState.Frozen) {
//       target.play();
//     }
//   } else if (target.currentState === PlayerState.Playing) {
//     target._cachedFrameReady.clear();
//     target._cachedFrames.clear();
//     target.freeze();
//     target.dispatchEvent(new CustomEvent(PlayerEvent.Freeze));
//   }
// }

@customElement('lottie-player')
export class LottiePlayer extends LottiePlayerModel {
  private TVG?: any;
  private canvas?: HTMLCanvasElement;
  private imageData?: ImageData;
  private beginTime: number = Date.now();
  private _counter: number = 1;
  public _cachedFrames: Map<Number, any> = new Map();
  public _cachedFrameReady: Map<Number, Boolean> = new Map();
  public _worker?: Worker;

  // private _timer: any;
  private _observer?: IntersectionObserver;

  constructor() {
    super();
    this._init();
  }

  private async _init(): Promise<void> {
    if (!_tvg) {
      // throw new Error('ThorVG has not loaded');
      return;
    }

    // this.TVG = _tvg;
  }

  // private _delayedLoad(): void {
  //   // if (!_wasmModule) {
  //   //   return;
  //   // }

  //   // clearInterval(this._timer);
  //   // this._timer = null;

  //   this.TVG = {
  //     load: (data: Uint8Array, rPath: string, width: number, height: number): Promise<boolean> => {
  //       return new Promise<boolean>((resolve, reject) => {
  //         try {
  //           this._worker!.postMessage({ type: "load", data, rPath, width, height });
            
  //           const _callback =  (e: any) => {
  //             const data = e.data as any;
  //             if (data.type === "loaded") {
  //               this._worker?.removeEventListener('message', _callback);
  //               resolve(true);
  //             } else {
  //               // reject(data.err);
  //             }
  //           };
  //           this._worker!.addEventListener('message', _callback);

  //           // const isLoaded = _tvg.load(data, 'lottie', this.canvas!.width, this.canvas!.height, rPath);
  //           // resolve(isLoaded);
  //         } catch (err) {
  //           reject(err);
  //         }
  //       });
  //     },
  //     error: (): Promise<string> => {
  //       return new Promise<string>((resolve, reject) => {
  //         try {
  //           const err = _tvg.error();
  //           resolve(err);
  //         } catch (err) {
  //           reject(err);
  //         }
  //       });
  //     },
  //     resize: (width: number, height: number): Promise<void> => {
  //       return new Promise<void>((resolve, reject) => {
  //         try {
  //           this._worker!.postMessage({ type: "resize", width, height });
  //           const _callback = (e: any) => {
  //             const data = e.data as any;
  //             if (data.type === "resized") {
  //               this._worker?.removeEventListener('message', _callback);
  //               resolve();
  //             } else {
  //               // reject(data.err);
  //             }
  //           };
  //           this._worker!.addEventListener('message', _callback);
  //           // _tvg.resize(width, height);
  //           // resolve();
  //         } catch (err) {
  //           reject(err);
  //         }
  //       });
  //     },
  //     render: (): Promise<Uint8Array> => {
  //       return new Promise<Uint8Array>((resolve, reject) => {
  //         try {
  //           this._worker!.postMessage({ type: "render" });
  //           const _callback = (e: any) => {
  //             const data = e.data as any;
  //             if (data.type === "rendered") {
  //               this._worker?.removeEventListener('message', _callback);
  //               resolve(e.data.buffer);
  //             } else {
  //               // reject(data.err);
  //             }
  //           };

  //           this._worker!.addEventListener('message', _callback);

  //           // const buf = _tvg.render();
  //           // resolve(buf);
  //         } catch (err) {
  //           reject(err);
  //         }
  //       });
  //     },
  //     update: (): Promise<boolean> => {
  //       return new Promise<boolean>((resolve, reject) => {
  //         try {
  //           this._worker!.postMessage({ type: "update" });
  //           const _callback = (e: any) => {
  //             const data = e.data as any;
  //             if (data.type === "updated") {
  //               this._worker?.removeEventListener('message', _callback);
  //               resolve(e.data.isUpdated);
  //             } else {
  //               // reject(data.err);
  //             }
  //           };

  //           this._worker!.addEventListener('message', _callback);

  //           // const isUpdated = _tvg.update();
  //           // resolve(isUpdated);
  //         } catch (err) {
  //           reject(err);
  //         }
  //       });
  //     },
  //     duration: (): Promise<number> => {
  //       return new Promise<number>((resolve, reject) => {
  //         try {
  //           this._worker!.postMessage({ type: "duration" });
  //           const _callback = (e: any) => {
  //             const data = e.data as any;
  //             if (data.type === "duration") {
  //               this._worker?.removeEventListener('message', _callback);
  //               resolve(e.data.duration);
  //             } else {
  //               // reject(data.err);
  //             }
  //           };
            
  //           this._worker!.addEventListener('message', _callback);

  //           // const duration = _tvg.duration();
  //           // resolve(duration);
  //         } catch (err) {
  //           reject(err);
  //         }
  //       });
  //     },
  //     frame: (frame: number): Promise<boolean> => {
  //       return new Promise<boolean>((resolve, reject) => {
  //         try {
  //           this._worker!.postMessage({ type: "frame", frame });
  //           const _callback = (e: any) => {
  //             const data = e.data as any;
  //             if (data.type === "frame") {
  //               this._worker?.removeEventListener('message', _callback);
  //               resolve(e.data.isUpdated);
  //             } else {
  //               // reject(data.err);
  //             }
  //           };

  //           this._worker!.addEventListener('message', _callback);

  //           // const isUpdated = _tvg.frame(frame);
  //           // resolve(isUpdated);
  //         } catch (err) {
  //           reject(err);
  //         }
  //       });
  //     },
  //     save2Gif: (data: Uint8Array, name: string, width: number, height: number, fps: number): Promise<boolean> => {
  //       return new Promise<boolean>((resolve, reject) => {
  //         try {
  //           const isSaved = _tvg.save2Gif(data, name, width, height, fps);
  //           resolve(isSaved);
  //         } catch (err) {
  //           reject(err);
  //         }
  //       });
  //     },
  //     totalFrame: (): Promise<number> => {
  //       return new Promise<number>((resolve, reject) => {
  //         try {
  //           this._worker!.postMessage({ type: "totalFrame" });
  //           const _callback = (e: any) => {
  //             const data = e.data as any;
  //             if (data.type === "totalFrame") {
  //               this._worker?.removeEventListener('message', _callback);
  //               resolve(e.data.totalFrame);
  //             } else {
  //               // reject(data.err);
  //             }
  //           };

  //           this._worker!.addEventListener('message', _callback);

  //           // const totalFrame = _tvg.totalFrame();
  //           // resolve(totalFrame);
  //         } catch (err) {
  //           reject(err);
  //         }
  //       });
  //     },
  //   };

  //   if (this.src) {
  //     this.load(this.src);
  //   }
  // }

  protected firstUpdated(_changedProperties: PropertyValueMap<any> | Map<PropertyKey, unknown>): void {
    this.canvas = this.shadowRoot!.querySelector('#thorvg-canvas') as HTMLCanvasElement;
    this.canvas.width = this.canvas.offsetWidth;
    this.canvas.height = this.canvas.offsetHeight;
    

    // this._observer = new IntersectionObserver(_observerCallback);
    // this._observer.observe(this);


    if (!this._worker) {
      this._worker = new Worker('/web/dist/worker.js');
      const that = this;
      this._worker.onmessage = function(e) {
        console.log(e.data.type);

        switch(e.data.type) {
          // case "wasmModule":
          //   _wasmModule = e.data.WasmModule;
          //   // @ts-ignore
          //   window.wasmModule = _wasmModule;
          //   break;
          case "loadedWasm":
            _tvg = e.data.tvg;
            // that._delayedLoad();


            if (that.src) {
              that.load(that.src);
            }
            break;
          // case "update":
          //   const buffer = e.data as Uint8ClampedArray;
          //   that.imageData = new ImageData(buffer, that.canvas!.width, that.canvas!.height);
          //   that._flush();
            // window.requestAnimationFrame(that._animLoop.bind(that));
            
            // break;
          // case "render":
          //   break;
        }
      }

      this._worker.onerror = function(e) {
        console.log("worker err: " + e );
      }
    }

    if (!_tvg) {
      this._worker.postMessage({
        type: 'loadWasm',
      });
    }

    const offscreen = this.canvas.transferControlToOffscreen();
    this._worker.postMessage({
      type: 'loadCanvas',
      offscreen: offscreen,
    }, [offscreen]);

    // if (this.src) {
    //   if (this.TVG) {

    //     this.load(this.src);
    //   } else {
    //     this._timer = setInterval(this._delayedLoad.bind(this), 100);
    //   }
    // }
  }

  protected createRenderRoot(): HTMLElement | DocumentFragment {
    this.style.display = 'block';
    return super.createRenderRoot();
  }

  // private async _animLoop(){
  //   if (!this.TVG) {
  //     return;
  //   }

  //   // if (await this._update()) {
  //   //   // this._worker?.postMessage({ type: "render", TVG: _tvg });
  //   //   this._render();
  //   //   window.requestAnimationFrame(this._animLoop.bind(this));
  //   // }
  // }

  private async _loadBytes(data: Uint8Array, rPath: string = ''): Promise<void> {
    this._worker?.postMessage({ type: "libLoad", data, rPath, width: this.canvas!.width, height: this.canvas!.height });

    // const isLoaded = await this.TVG.load(data, rPath, this.canvas!.width, this.canvas!.height);
    // if (!isLoaded) {
    //   const err = await this.TVG.error();
    //   throw new Error('Unable to load an image. Error: ', err);
    // }

    // this._worker?.postMessage({ type: "libRender" });
    // // await this._render();
    // this.dispatchEvent(new CustomEvent(PlayerEvent.Load));
    
    // if (this.autoPlay) {
    //   this._worker?.postMessage({ type: "libPlay" });
    //   // await this.play();
    // }
  }

  private _flush(): void {
    // const currentFrame = Math.floor(this.currentFrame);

    const context = this.canvas!.getContext('2d');
    context!.putImageData(this.imageData!, 0, 0);

    // this._worker?.postMessage({ type: "compress", buffer: this.canvas! });

    // this._cachedFrames.set(currentFrame, this.canvas!.toDataURL());
  }

  private async _render(): Promise<void> {
    console.log("render");
    // const currentFrame = Math.floor(this.currentFrame);
    // if (this._cachedFrames.has(currentFrame)) {
    //   const buffer = this._cachedFrames.get(currentFrame)!;
    //   this._worker?.postMessage({ type: "decode", buffer });

    //   // const clampedBuffer = Uint8ClampedArray.from(buffer);
    //   // this.imageData = new ImageData(clampedBuffer, this.canvas!.width, this.canvas!.height);
    //   // this._flush();
    //   return;
    // }

    await this.TVG.resize(this.canvas!.width, this.canvas!.height);
    const isUpdated = await this.TVG.update();

    if (!isUpdated) {
      return;
    }


    // const hasFrameCache = this._cachedFrameReady.has(currentFrame);
    // if (!hasFrameCache) {
    //   this._cachedFrameReady.set(currentFrame, true);
    // }


    const buffer = await this.TVG.render();
    const clampedBuffer = Uint8ClampedArray.from(buffer);
    if (clampedBuffer.length < 1) {
      return;
    }

    // if (!hasFrameCache) {
    //   this._worker?.postMessage({ type: "compress", frame: currentFrame, buffer: clampedBuffer });
    // }

    this.imageData = new ImageData(clampedBuffer, this.canvas!.width, this.canvas!.height);
    this._flush();
  }

  private async _update(): Promise<boolean> {
    if (this.currentState !== PlayerState.Playing) {
      return false;
    }

    const duration = await this.TVG.duration();
    const currentTime = Date.now() / 1000;
    this.currentFrame = (currentTime - this.beginTime) / duration * this.totalFrame * this.speed;
    if (this.direction === -1) {
      this.currentFrame = this.totalFrame - this.currentFrame;
    }

    if (
      (this.direction === 1 && this.currentFrame >= this.totalFrame) ||
      (this.direction === -1 && this.currentFrame <= 0)
    ) {
      const totalCount = this.count ? this.mode === PlayMode.Bounce ? this.count * 2 : this.count : 0;
      if (this.loop || (totalCount && this._counter < totalCount)) {
        if (this.mode === PlayMode.Bounce) {
          this.direction = this.direction === 1 ? -1 : 1;
          this.currentFrame = this.direction === 1 ? 0 : this.totalFrame;
        }

        if (this.count) {
          this._counter += 1;
        }

        await _wait(this.intermission);
        await this.play();
        return true;
      }

      this.dispatchEvent(new CustomEvent(PlayerEvent.Complete));
      this.currentState = PlayerState.Stopped;
    }

    this.dispatchEvent(new CustomEvent(PlayerEvent.Frame, {
      detail: {
        frame: this.currentFrame,
      },
    }));
    return await this.TVG.frame(this.currentFrame);
  }

  private async _frame(curFrame: number): Promise<void> {
    this.pause();
    this.currentFrame = curFrame;
    await this.TVG.frame(curFrame);
  }

  public async load(src: string | object): Promise<void> {
    try {
      const bytes = await _parseSrc(src);
      this.dispatchEvent(new CustomEvent(PlayerEvent.Ready));

      await this._loadBytes(bytes);
    } catch (err) {
      this.currentState = PlayerState.Error;
      this.dispatchEvent(new CustomEvent(PlayerEvent.Error));
    }
  }

  public async play(): Promise<void> {
    // this.totalFrame = await this.TVG.totalFrame();
    // if (this.totalFrame < 1) {
    //   return;
    // }

    // this.beginTime = Date.now() / 1000;
    // if (this.currentState == PlayerState.Playing) {
    //   return;
    // }

    // this.currentState = PlayerState.Playing;
    // window.requestAnimationFrame(this._animLoop.bind(this));
  }

  public pause(): void {
    this.currentState = PlayerState.Paused;
    this.dispatchEvent(new CustomEvent(PlayerEvent.Pause));
  }

  public stop(): void {
    this.currentState = PlayerState.Stopped;
    this.currentFrame = 0;
    this._counter = 1;
    this.TVG.frame(0);

    this.dispatchEvent(new CustomEvent(PlayerEvent.Stop));
  }

  public freeze(): void {
    this.currentState = PlayerState.Frozen;
    this.dispatchEvent(new CustomEvent(PlayerEvent.Freeze));
  }

  public async seek(frame: number): Promise<void> {
    await this._frame(frame);
    await this._update();
    await this._render();
  }

  public destroy(): void {
    if (!this.TVG) {
      return;
    }

    this.TVG = null;
    this.currentState = PlayerState.Destroyed;

    if (this._observer) {
      this._observer.disconnect();
      this._observer = undefined;
    }
    
    this.dispatchEvent(new CustomEvent(PlayerEvent.Destroyed));
    this.remove();
  }

  public setLooping(value: boolean): void {
    if (!this.TVG) {
      return;
    }

    this.loop = value;
  }

  public setDirection(value: number): void {
    if (!this.TVG) {
      return;
    }

    this.direction = value;
  }

  public setSpeed(value: number): void {
    if (!this.TVG) {
      return;
    }

    this.speed = value;
  }

  public setBgColor(value: string): void {
    if (!this.TVG) {
      return;
    }

    this.canvas!.style.backgroundColor = value;
  }

  public async save(target: ExportableType): Promise<void> {
    if (!this.TVG || !this.src) {
      return;
    }

    const bytes = await _parseSrc(this.src);
    
    switch (target) {
      case ExportableType.GIF:
        const isExported = this.TVG.save2Gif(bytes, 'lottie', this.canvas!.width, this.canvas!.height, 30);
        if (!isExported) {
          throw new Error('Unable to save a GIF. Error: ', this.TVG.error());
        }

        const data = _module.FS.readFile('output.gif');
        if (data.length < 6) {
          throw new Error("Unable to save the Gif data. The generated file size is invalid.");
        }

        const blob = new Blob([data], {type: 'application/octet-stream'});
        const link = document.createElement("a");
        link.setAttribute('href', URL.createObjectURL(blob));
        link.setAttribute('download', 'output.gif');
        document.body.appendChild(link);
        link.click();
        document.body.removeChild(link);
        break;
    }
  }

  public getVersion(): LibraryVersion {
    return {
      THORVG_VERSION,
    };
  }

  public render (): TemplateResult {
    return html`
      <canvas id="thorvg-canvas" style="width: 100%; height: 100%;" />
    `;
  }
}
