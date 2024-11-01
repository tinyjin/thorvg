/*
 * Copyright (c) 2024 the ThorVG project. All rights reserved.
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

/************************************************************************/
/* ThorVG Drawing Contents                                              */
/************************************************************************/

#define NUM_PER_ROW 2
#define NUM_PER_COL 2

struct UserExample : tvgexam::Example
{
    unique_ptr<tvg::LottieAnimation> slot1;
    unique_ptr<tvg::LottieAnimation> slot2;
    unique_ptr<tvg::LottieAnimation> slot3;
    unique_ptr<tvg::LottieAnimation> marker;
    uint32_t w, h;
    uint32_t size;

    void sizing(tvg::Picture* picture, uint32_t counter)
    {
        //image scaling preserving its aspect ratio
        float scale;
        float shiftX = 0.0f, shiftY = 0.0f;
        float w, h;
        picture->size(&w, &h);

        if (w > h) {
            scale = size / w;
            shiftY = (size - h * scale) * 0.5f;
        } else {
            scale = size / h;
            shiftX = (size - w * scale) * 0.5f;
        }

        picture->scale(scale);
        picture->translate((counter % NUM_PER_ROW) * size + shiftX, (counter / NUM_PER_ROW) * (this->h / NUM_PER_COL) + shiftY);
    }

    bool update(tvg::Canvas* canvas, uint32_t elapsed) override
    {
        if (!canvas) return false;

        //gradient slot
        {
            auto progress = tvgexam::progress(elapsed, slot1->duration());
            slot1->frame(slot1->totalFrame() * progress);
        }

        //solid fill slot
        {
            auto progress = tvgexam::progress(elapsed, slot2->duration());
            slot2->frame(slot2->totalFrame() * progress);
        }

        //image slot
        {
            auto progress = tvgexam::progress(elapsed, slot3->duration());
            slot3->frame(slot3->totalFrame() * progress);
        }

        //marker
        {
            auto progress = tvgexam::progress(elapsed, marker->duration());
            marker->frame(marker->totalFrame() * progress);
        }

        canvas->update();

        return true;
    }

    bool content(tvg::Canvas* canvas, uint32_t w, uint32_t h) override
    {
        if (!canvas) return false;

        //Background
        auto shape = tvg::Shape::gen();
        shape->appendRect(0, 0, w, h);
        shape->fill(75, 75, 75);

        canvas->push(std::move(shape));

        this->w = w;
        this->h = h;
        this->size = w / NUM_PER_ROW;

        //slot (gradient)
        {
            slot1 = tvg::LottieAnimation::gen();
            auto picture = slot1->picture();
            if (!tvgexam::verify(picture->load(EXAMPLE_DIR"/lottie/extensions/slotsample.json"))) return false;

            const char* slotJson = R"({"gradient_fill":{"p":{"a":0,"k":[0,0.1,0.1,0.2,1,1,0.1,0.2,0.1,1]}}})";
            if (!tvgexam::verify(slot1->override(slotJson))) return false;

            sizing(picture, 0);

            canvas->push(tvg::cast(picture));
        }

        //slot (solid fill)
        {
            slot2 = tvg::LottieAnimation::gen();
            auto picture = slot2->picture();
            if (!tvgexam::verify(picture->load(EXAMPLE_DIR"/lottie/extensions/slotsample2.json"))) return false;

            const char* slotJson = R"({"ball_color":{"p":{"a":1,"k":[{"i":{"x":[0.833],"y":[0.833]},"o":{"x":[0.167],"y":[0.167]},"t":7,"s":[0,0.176,0.867]},{"i":{"x":[0.833],"y":[0.833]},"o":{"x":[0.167],"y":[0.167]},"t":22,"s":[0.867,0,0.533]},{"i":{"x":[0.833],"y":[0.833]},"o":{"x":[0.167],"y":[0.167]},"t":37,"s":[0.867,0,0.533]},{"t":51,"s":[0,0.867,0.255]}]}}})";
            if (!tvgexam::verify(slot2->override(slotJson))) return false;

            sizing(picture, 1);

            canvas->push(tvg::cast(picture));
        }

        //slot (image)
        {
            slot3 = tvg::LottieAnimation::gen();
            auto picture = slot3->picture();
            if (!tvgexam::verify(picture->load(EXAMPLE_DIR"/lottie/extensions/slotsample3.json"))) return false;

            const char* slotJson = R"({"sun_img":{"p":{"id":"image_0","w":1362,"h":1362,"u":"","p":"data:image/png;base64,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","e":1}}})";
            // if (!tvgexam::verify(slot3->override(slotJson))) return false;
            slot3->override(slotJson);

            sizing(picture, 2);

            canvas->push(tvg::cast(picture));
        }

        //marker
        {
            marker = tvg::LottieAnimation::gen();
            auto picture = marker->picture();
            if (!tvgexam::verify(picture->load(EXAMPLE_DIR"/lottie/extensions/marker_sample.json"))) return false;
            if (!tvgexam::verify(marker->segment("sectionC"))) return false;

            sizing(picture, 3);

            canvas->push(tvg::cast(picture));
        }

        return true;
    }
};


/************************************************************************/
/* Entry Point                                                          */
/************************************************************************/

int main(int argc, char **argv)
{
    return tvgexam::main(new UserExample, argc, argv);
}