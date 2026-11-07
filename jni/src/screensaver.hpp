#ifndef SCREENSAVER_HPP
#define SCREENSAVER_HPP

#include <memory>
#include "scene_interface.hpp"

// Whether the screensaver should be showing. On Android the activity's idle
// timer turns it on and off (PrinterTablet.nativeSetScreensaver); elsewhere it
// is always off.
bool ScreensaverActive();

// Shows `content` normally and `saver` while the screensaver is active.
// `content` keeps updating underneath so it is current when the screen wakes.
class ScreensaverSwitch : public SceneInterface
{
    std::shared_ptr<SceneInterface> content;
    std::shared_ptr<SceneInterface> saver;
    bool active;

public:
    ScreensaverSwitch(std::shared_ptr<SceneInterface> content, std::shared_ptr<SceneInterface> saver)
    : content(content), saver(saver), active(false)
    {
    }

    virtual void Init(SDL_Window* window)
    {
        content->Init(window);
        saver->Init(window);
    }

    virtual void Update(const InputState& inputs, Uint32 timestamp)
    {
        active = ScreensaverActive();
        content->Update(inputs, timestamp);
        if (active)
        {
            saver->Update(inputs, timestamp);
        }
    }

    virtual void Render(std::shared_ptr<RenderContextInterface> renderContext)
    {
        (active ? saver : content)->Render(renderContext);
    }

    virtual bool Running() const
    {
        return content->Running();
    }
};

#endif // SCREENSAVER_HPP
