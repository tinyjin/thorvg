import { LitElement } from 'lit';
import { property } from 'lit/decorators.js';

// Define library version
export interface LibraryVersion {
  THORVG_VERSION: string
}

// Define file type which can be exported
export enum ExportableType {
  GIF = 'gif',
}

// Define valid player states
export enum PlayerState {
  Destroyed = "destroyed",
  Error = "error",
  Loading = "loading",
  Paused = "paused",
  Playing = "playing",
  Stopped = "stopped",
}

// Define play modes
export enum PlayMode {
  Bounce = "bounce",
  Normal = "normal",
}

// Define player events
export enum PlayerEvent {
	Complete = "complete",
  Destroyed = "destroyed",
  Error = "error",
  Frame = "frame",
  Load = "load",
  Loop = "loop",
  Pause = "pause",
  Play = "play",
  Ready = "ready",
  Stop = "stop",
}

export abstract class LottiePlayerModel extends LitElement {
  /**
   * LottieFiles JSON data or URL to JSON.
   */
  @property({ type: String })
  public src?: string;

  /**
   * Animation speed.
   */
  @property({ type: Number })
  public speed: number = 1.0;

  /**
   * Autoplay animation on load.
   */
  @property({ type: Boolean })
  public autoPlay: boolean = false;

  /**
   * Number of times to loop animation.
   */
  @property({ type: Number })
  public count?: number;

  /**
   * Whether to loop animation.
   */
  @property({ type: Boolean })
  public loop: boolean = false;

  /**
   * Direction of animation.
   */
  @property({ type: Number })
  public direction: number = 1;

  /**
   * Play mode.
   */
  @property()
  public mode: PlayMode = PlayMode.Normal;

  /**
   * Intermission
   */
  @property()
  public intermission: number = 1;

  /**
   * total frame of current animation
   */
  @property({ type: Number })
  public totalFrame: number = 0;

  /**
   * current frame of current animation
   */
  @property({ type: Number })
  public currentFrame: number = 0;

  /**
   * Player state.
   */
  @property({ type: Number })
  public currentState: PlayerState = PlayerState.Loading;

  /**
   * Configure and load
   */
  public abstract load (src: string | object): Promise<void>;

  /**
   * Start playing animation.
   */
  public abstract play (): void;

  /**
   * Pause animation.
   */
  public abstract pause (): void;

  /**
   * Stop animation.
   */
  public abstract stop (): void;

  /**
   * Seek to a given frame
   *
   * @param frame Frame number to move
   */
  public abstract seek (frame: number): void;

  /**
   * Destroy animation and lottie-player element.
   */
  public abstract destroy (): void;

  /**
   * Sets the repeating of the animation.
   *
   * @param value Whether to enable repeating. Boolean true enables repeating.
   */
  public abstract setLooping (value: boolean): void;

  /**
   * Animation play direction.
   *
   * @param value Direction values.
   */
  public abstract setDirection (value: number): void;

  /**
   * Sets animation play speed.
   *
   * @param value Playback speed.
   */
  public abstract setSpeed (value: number): void;

  /**
   * Set a background color.
   *
   * The default color is full-transparency.
   *
   * @param value Hex(#fff) or string(red) of background color
   */
  public abstract setBgColor (value: string): void;

  /**
   * Save current animation to other file type
   *
   * @param target File type to export
   */
  public abstract save (target: ExportableType): void;

  /**
   * Returns thorvg version
   */
  public abstract getVersions (): LibraryVersion;
}
