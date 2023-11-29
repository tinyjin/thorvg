import { html, PropertyValueMap, type TemplateResult } from 'lit';
import { customElement } from 'lit/decorators.js';

// @ts-ignore: WASM Glue code doesn't have type & Only available on build progress
import Module from '../dist/thorvg-wasm';
import { ExportableType, LibraryVersion, LottiePlayerModel, PlayerEvent, PlayerState } from './lottie-player.model';

let _tvg: any;
(async () => {  
  const module = await Module();
  _tvg = new module.TvgLottieAnimation();
})();

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

@customElement('lottie-player')
export class LottiePlayer extends LottiePlayerModel {
  private TVG?: any;
  private canvas?: HTMLCanvasElement;
  private imageData?: ImageData;
  private beginTime: number = Date.now();

  private _timer: any;

  constructor() {
    super();
    this._init();
  }

  private async _init(): Promise<void> {
    if (!_tvg) {
      // throw new Error('ThorVG has not loaded');
      return;
    }

    this.TVG = _tvg;
  }

  private _delayedLoad(): void {
    if (!_tvg || !this._timer) {
      return;
    }

    clearInterval(this._timer);
    this._timer = null;

    this.TVG = _tvg;

    if (this.src) {
      this.load(this.src);
    }
  }

  protected firstUpdated(_changedProperties: PropertyValueMap<any> | Map<PropertyKey, unknown>): void {
    this.canvas = this.shadowRoot!.querySelector('#thorvg-canvas') as HTMLCanvasElement;
    this.canvas.width = this.canvas.offsetWidth;
    this.canvas.height = this.canvas.offsetHeight;

    if (this.src) {
      if (this.TVG) {
        this.load(this.src);
      } else {
        this._timer = setInterval(this._delayedLoad.bind(this), 100);
      }
    }
  }

  protected createRenderRoot(): HTMLElement | DocumentFragment {
    this.style.display = 'block';
    return super.createRenderRoot();
  }

  private _animLoop() {
    if (!this.TVG) {
      return;
    }

    if (this._update()) {
      this._render();
      window.requestAnimationFrame(this._animLoop.bind(this));
    }
  }

  private _loadBytes(data: Uint8Array, rPath: string = ''): void {
    const isLoaded = this.TVG.load(data, 'lottie', this.canvas!.width, this.canvas!.height, rPath);
    if (!isLoaded) {
      throw new Error('Unable to load an image. Error: ', this.TVG.error());
    }

    this._render();
    this.dispatchEvent(new CustomEvent(PlayerEvent.Load));
    
    if (this.autoPlay) {
      this.play();
    }
  }

  private _flush(): void {
    const context = this.canvas!.getContext('2d');
    context!.putImageData(this.imageData!, 0, 0);
  }

  private _render(): void {
    this.TVG.resize(this.canvas!.width, this.canvas!.height);
    const isUpdated = this.TVG.update();

    if (!isUpdated) {
      return;
    }

    const buffer = this.TVG.render();
    const clampedBuffer = Uint8ClampedArray.from(buffer);
    if (clampedBuffer.length < 1) {
      return;
    }

    this.imageData = new ImageData(clampedBuffer, this.canvas!.width, this.canvas!.height);
    this._flush();
  }

  private _update(): boolean {
    if (this.currentState !== PlayerState.Playing) {
      return false;
    }

    const duration = this.TVG.duration();
    const currentTime = Date.now() / 1000;
    this.currentFrame = (currentTime - this.beginTime) / duration * this.totalFrame * this.speed;
    if (this.direction === -1) {
      this.currentFrame = this.totalFrame - this.currentFrame;
    }

    if (this.currentFrame >= this.totalFrame) {
      this.dispatchEvent(new CustomEvent(PlayerEvent.Complete));

      if (this.loop) {
        this.play();
        return true;
      } else {
        this.currentState = PlayerState.Stopped;
      }
    }

    this.dispatchEvent(new CustomEvent(PlayerEvent.Frame, {
      detail: {
        frame: this.currentFrame,
      },
    }));
    return this.TVG.frame(this.currentFrame);
  }

  private _frame(curFrame: number): void {
    this.pause();
    this.currentFrame = curFrame;
    this.TVG.frame(curFrame);
  }

  public async load(src: string | object): Promise<void> {
    try {
      const bytes = await _parseSrc(src);
      this.dispatchEvent(new CustomEvent(PlayerEvent.Ready));

      this._loadBytes(bytes);
    } catch (err) {
      this.currentState = PlayerState.Error;
      this.dispatchEvent(new CustomEvent(PlayerEvent.Error));
    }
  }

  public play(): void {
    this.totalFrame = this.TVG.totalFrame();
    if (this.totalFrame < 1) {
      return;
    }

    this.beginTime = Date.now() / 1000;
    if (this.currentState == PlayerState.Playing) {
      return;
    }

    this.currentState = PlayerState.Playing;
    window.requestAnimationFrame(this._animLoop.bind(this));
  }

  public pause(): void {
    this.currentState = PlayerState.Paused;
    this.dispatchEvent(new CustomEvent(PlayerEvent.Pause));
  }

  public stop(): void {
    this.currentState = PlayerState.Stopped;
    this.currentFrame = 0;
    this.TVG.frame(0);

    this.dispatchEvent(new CustomEvent(PlayerEvent.Stop));
  }

  public seek(frame: number): void {
    this._frame(frame);
    this._update();
    this._render();
  }

  public destroy(): void {
    if (!this.TVG) {
      return;
    }

    this.TVG = null;
    this.currentState = PlayerState.Destroyed;
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

  public setBgColor(value: number): void {
    throw new Error('Method not implemented.');
  }

  public save(target: ExportableType): void {
    throw new Error('Method not implemented.');
  }

  public getVersions(): LibraryVersion {
    throw new Error('Method not implemented.');
  }

  public render (): TemplateResult {
    return html`
      <canvas id="thorvg-canvas" style="width: 100%; height: 100%;" />
    `;
  }
}
