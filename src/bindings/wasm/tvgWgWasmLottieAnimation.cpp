/*
 * Copyright (c) 2020 - 2024 the ThorVG project. All rights reserved.

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <emscripten/bind.h>
#include <webgpu/webgpu.h>
#include <thorvg.h>
#include <iostream>

class TvgWgLottieAnimation
{
public:
    TvgWgLottieAnimation()
    { 
        tvg::Initializer::init(0, tvg::CanvasEngine::Wg);
        canvas = tvg::WgCanvas::gen();
    }

    ~TvgWgLottieAnimation()
    {
        wgpuSurfaceRelease(surface);
        wgpuInstanceRelease(instance);
        tvg::Initializer::term();
     }

    void resize(uint32_t width, uint32_t height)
    {
        // instance
        instance = wgpuCreateInstance(nullptr);

        // surface
        WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
        canvasDesc.chain.next = nullptr;
        canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
        canvasDesc.selector = "#thorvg-canvas";
        WGPUSurfaceDescriptor surfaceDesc{};
        surfaceDesc.nextInChain = &canvasDesc.chain;
        surface = wgpuInstanceCreateSurface(instance, &surfaceDesc);

        // store size
        this->width = width;
        this->height = height;

        // init canvas target
        canvas->target(this->instance, this->surface, width, height);
    }

    void render()
    {
        if (canvas->draw() == tvg::Result::Success) {
            canvas->sync();
        }
    }

    bool frame(float no)
    {
        if(!animation) return false;
        animation->frame(no);
        return true;
    }

    bool update()
    {
        canvas->update();
        return true;
    }

    float duration()
    {
        if (!canvas || !animation) return 0;
        return animation->duration();
    }

    float totalFrame()
    {
        if (!canvas || !animation) return 0;
        return animation->totalFrame();
    }

    bool load(std::string data, std::string mimetype)
    {
        // clear canvas
        canvas->clear(true);

        // create new animation
        animation = tvg::Animation::gen();

        // load data to animation
        if (animation->picture()->load(data.c_str(), data.size(), mimetype) != tvg::Result::Success) {
            std::cout << "animation->picture()->load(...) failed" << std::endl;
            return false;
        }

        float w{}, h{};
        animation->picture()->size(&w, &h);

        float scale{};
        float shiftX = 0.0f, shiftY = 0.0f;
        if (w > h) {
            scale = width / w;
            shiftY = (height - h * scale) * 0.5f;
        } else {
            scale = height / h;
            shiftX = (width - w * scale) * 0.5f;
        }
        animation->picture()->scale(scale);
        animation->picture()->translate(shiftX, shiftY);

        // push animation to canvas
        if (canvas->push(cast(animation->picture())) != tvg::Result::Success) {
            std::cout << "canvas->push(cast(animation->picture())) failed" << std::endl;
            return false;
        }

        return true;
    }

private:
    WGPUInstance instance{};
    WGPUSurface surface{};
    uint32_t width{};
    uint32_t height{};

    std::unique_ptr<tvg::WgCanvas> canvas{};
    std::unique_ptr<tvg::Animation> animation{};
};

EMSCRIPTEN_BINDINGS(thorvg_bindings) {
    emscripten::class_<TvgWgLottieAnimation>("TvgWgLottieAnimation")
        .constructor()
        .function("resize", &TvgWgLottieAnimation::resize)
        .function("render", &TvgWgLottieAnimation::render)
        .function("frame", &TvgWgLottieAnimation::frame)
        .function("update", &TvgWgLottieAnimation::update)
        .function("duration", &TvgWgLottieAnimation::duration)
        .function("totalFrame", &TvgWgLottieAnimation::totalFrame)
        .function("load", &TvgWgLottieAnimation::load);
}
