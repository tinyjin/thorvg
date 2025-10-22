/*
 * Copyright (c) 2023 - 2025 the ThorVG project. All rights reserved.

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

#include <thorvg_lottie.h>
#include "Example.h"
#include "iostream"

/************************************************************************/
/* ThorVG Drawing Contents                                              */
/************************************************************************/

struct UserExample : tvgexam::Example
{
    unique_ptr<tvg::LottieAnimation> animation;

    bool content(tvg::Canvas* canvas, uint32_t w, uint32_t h) override
    {
        //The default font for fallback in case
        tvg::Text::load(EXAMPLE_DIR"/font/Arial.ttf");

        //Animation Controller
        animation = unique_ptr<tvg::LottieAnimation>(tvg::LottieAnimation::gen());
        auto picture = animation->picture();
        picture->origin(0.5f, 0.5f);  //center origin

        //Background
        auto shape = tvg::Shape::gen();
        shape->appendRect(0, 0, w, h);
        shape->fill(50, 50, 50);

        canvas->push(shape);

        if (!tvgexam::verify(picture->load(EXAMPLE_DIR"/lottie/extensions/slot12.json"))) return false;

        //slot (effect: slider control)
        const char* slotJson = R"({"slider_control":{"p":{"a":1,"k":[{"i":{"x":[0.833],"y":[0.833]},"o":{"x":[0.167],"y":[0.167]},"t":0,"s":[0],"e":[50]},{"t":100}]}}})";
        auto slotId = animation->gen(slotJson);
        std::cout << "slotId: " << slotId << std::endl;
        if (!tvgexam::verify(animation->apply(slotId))) return false;

        //image scaling preserving its aspect ratio
        float w2, h2;
        picture->size(&w2, &h2);
        auto scale = (w2 > h2) ? w / w2 : h / h2;
        picture->scale(scale);
        picture->translate(float(w) * 0.5f, float(h) * 0.5f);

        canvas->push(picture);

        return true;
    }

    bool update(tvg::Canvas* canvas, uint32_t elapsed) override
    {
        auto progress = tvgexam::progress(elapsed, animation->duration());

        //Update animation frame only when it's changed
        if (animation->frame(animation->totalFrame() * progress) == tvg::Result(0)) {
            canvas->update();
            return true;
        }

        return false;
    }
};


/************************************************************************/
/* Entry Point                                                          */
/************************************************************************/

int main(int argc, char **argv)
{
    return tvgexam::main(new UserExample, argc, argv, false, 1024, 1024, 1, true);
}
