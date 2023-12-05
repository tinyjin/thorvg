import { LitElement, html, type TemplateResult } from 'lit';
import { customElement } from 'lit/decorators.js';

// @ts-ignore: WASM Glue code doesn't have type & Only available on build progress
import Module from '../dist/thorvg-wasm';
let TVG: object | null = null;

(async () => {
  const module = await Module();
  TVG = new module.TvgLottieAnimation();
})();

@customElement('lottie-player')
export class LottiePlayer extends LitElement {
  public render (): TemplateResult {
    return html` <h1>Hello ThorVG!</h1> `;
  }
}
