import { LitElement } from 'lit';
import { property } from 'lit/decorators.js';

// Define library version
interface LibraryVersion {
  THORVG_VERSION: string
}

// Define file type which can be exported
enum ExportableType {
  GIF = 'gif',
}

export abstract class LottiePlayerModel extends LitElement {
  @property({ type: Boolean })
  public direction: number = 1;

  @property({ type: Number })
  public speed: number = 1.0;

  @property({ type: Number })
  public totalFrame?: number;

  @property({ type: Number })
  public currentFrame?: number;

  @property({ type: Boolean })
  public loop: boolean = false;

  /**
   * Start playing animation.
   */
  public abstract play (): void

  /**
   * Pause animation.
   */
  public abstract pause (): void

  /**
   * Stop animation.
   */
  public abstract stop (): void

  /**
   * Start playing animation.
   */
  public abstract load (src: string | object): void

  /**
   * Seek to a given frame
   *
   * @param frame Frame number to move
   */
  public abstract seek (frame: number): void

  /**
   * Sets the repeating of the animation.
   *
   * @param value Whether to enable repeating. Boolean true enables repeating.
   */
  public abstract setLooping (value: boolean): void

  /**
   * Animation play direction.
   *
   * @param value Direction values.
   */
  public abstract setDirection (value: number): void

  /**
   * Sets animation play speed.
   *
   * @param value Playback speed.
   */
  public abstract setSpeed (value: number): void

  /**
   * Set a background color.
   *
   * The default color is full-transparency.
   *
   * @param value Background color(ff00ff00)
   */
  public abstract setBgColor (value: number): void

  /**
   * Save current animation to other file type
   *
   * @param target File type to export
   */
  public abstract save (target: ExportableType): void

  /**
   * Returns thorvg version
   */
  public abstract getVersions (): LibraryVersion
}
