import { LitElement, PropertyValueMap, TemplateResult } from 'lit';

interface LibraryVersion {
    THORVG_VERSION: string;
}
declare enum ExportableType {
    GIF = "gif"
}
declare enum PlayerState {
    Destroyed = "destroyed",
    Error = "error",
    Frozen = "frozen",
    Loading = "loading",
    Paused = "paused",
    Playing = "playing",
    Stopped = "stopped"
}
declare enum PlayMode {
    Bounce = "bounce",
    Normal = "normal"
}
declare abstract class LottiePlayerModel extends LitElement {
    /**
     * LottieFiles JSON data or URL to JSON.
     */
    src?: string;
    /**
     * Animation speed.
     */
    speed: number;
    /**
     * Autoplay animation on load.
     */
    autoPlay: boolean;
    /**
     * Number of times to loop animation.
     */
    count?: number;
    /**
     * Whether to loop animation.
     */
    loop: boolean;
    /**
     * Direction of animation.
     */
    direction: number;
    /**
     * Play mode.
     */
    mode: PlayMode;
    /**
     * Intermission
     */
    intermission: number;
    /**
     * total frame of current animation
     */
    totalFrame: number;
    /**
     * current frame of current animation
     */
    currentFrame: number;
    /**
     * Player state.
     */
    currentState: PlayerState;
    /**
     * Configure and load
     */
    abstract load(src: string | object): Promise<void>;
    /**
     * Start playing animation.
     */
    abstract play(): void;
    /**
     * Pause animation.
     */
    abstract pause(): void;
    /**
     * Stop animation.
     */
    abstract stop(): void;
    /**
     * Freeze animation.
     */
    abstract freeze(): void;
    /**
     * Seek to a given frame
     *
     * @param frame Frame number to move
     */
    abstract seek(frame: number): void;
    /**
     * Destroy animation and lottie-player element.
     */
    abstract destroy(): void;
    /**
     * Sets the repeating of the animation.
     *
     * @param value Whether to enable repeating. Boolean true enables repeating.
     */
    abstract setLooping(value: boolean): void;
    /**
     * Animation play direction.
     *
     * @param value Direction values.
     */
    abstract setDirection(value: number): void;
    /**
     * Sets animation play speed.
     *
     * @param value Playback speed.
     */
    abstract setSpeed(value: number): void;
    /**
     * Set a background color.
     *
     * The default color is full-transparency.
     *
     * @param value Hex(#fff) or string(red) of background color
     */
    abstract setBgColor(value: string): void;
    /**
     * Save current animation to other file type
     *
     * @param target File type to export
     */
    abstract save(target: ExportableType): void;
    /**
     * Returns thorvg version
     */
    abstract getVersion(): LibraryVersion;
}

declare class LottiePlayer extends LottiePlayerModel {
    private TVG?;
    private canvas?;
    private imageData?;
    private beginTime;
    private _counter;
    private _timer;
    private _observer?;
    constructor();
    private _init;
    private _delayedLoad;
    protected firstUpdated(_changedProperties: PropertyValueMap<any> | Map<PropertyKey, unknown>): void;
    protected createRenderRoot(): HTMLElement | DocumentFragment;
    private _animLoop;
    private _loadBytes;
    private _flush;
    private _render;
    private _update;
    private _frame;
    load(src: string | object): Promise<void>;
    play(): void;
    pause(): void;
    stop(): void;
    freeze(): void;
    seek(frame: number): Promise<void>;
    destroy(): void;
    setLooping(value: boolean): void;
    setDirection(value: number): void;
    setSpeed(value: number): void;
    setBgColor(value: string): void;
    save(target: ExportableType): Promise<void>;
    getVersion(): LibraryVersion;
    render(): TemplateResult;
}

export { LottiePlayer };
