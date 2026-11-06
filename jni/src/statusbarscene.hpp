#ifndef STATUSBARSCENE_HPP
#define STATUSBARSCENE_HPP

#include <sstream>
#include <string>

#include "opengl.hpp"
#include "utils.hpp"
#include "defaultshader.hpp"
#include "quadcollectionvbo.hpp"
#include "vboscene.hpp"
#include "moonrakerclient.hpp"

// A full-width bar showing the Moonraker connection state. Place it with
// CompositeScene::AddScene(bar, 0, screenHeight - bar->GetHeight()).
class StatusBarScene : public ComposableSceneInterface
{
    std::shared_ptr<MoonrakerClient> client;
    int width, height;
    std::shared_ptr<TTF_Font> font;
    std::shared_ptr<SDL_Surface> tex;
    std::shared_ptr<ShaderInterface> shader;
    std::shared_ptr<QuadCollectionVbo> vbo;
    std::shared_ptr<VboScene> scene;
    std::string shownText;   // what the texture currently says; redraw only on change

    static int NextPowerOfTwo(int n)
    {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    static SDL_Color Color(Uint8 r, Uint8 g, Uint8 b)
    {
        SDL_Color c;
        c.r = r; c.g = g; c.b = b; c.a = 0xff;
        return c;
    }

    void Describe(const MoonrakerClient::Status& status, std::string& text, SDL_Color& dot) const
    {
        std::stringstream ss;
        ss << client->GetHost() << ":" << client->GetPort() << "   ";

        switch (status.state)
        {
        case MoonrakerClient::Connecting:
            ss << "Connecting...";
            dot = Color(0xe0, 0xc0, 0x30);
            break;
        case MoonrakerClient::Connected:
            ss << "Connected   Klipper: " << status.klippyState;
            if (!status.moonrakerVersion.empty())
            {
                ss << "   Moonraker " << status.moonrakerVersion;
            }
            dot = status.klippyState == "ready" ? Color(0x40, 0xd0, 0x60) : Color(0xe0, 0xc0, 0x30);
            break;
        case MoonrakerClient::Disconnected:
            ss << "Disconnected: " << status.error << " (retrying)";
            dot = Color(0xe0, 0x40, 0x40);
            break;
        }
        text = ss.str();
    }

    void Redraw(const std::string& text, SDL_Color dotColor)
    {
        SDL_FillRect(tex.get(), nullptr, SDL_MapRGBA(tex->format, 0x20, 0x20, 0x20, 0xff));

        int padding = height / 3;
        int x = padding;

        SDL_Surface* dot = TTF_RenderUTF8_Blended(font.get(), "\xE2\x97\x8F", dotColor);   // U+25CF ●
        if (dot)
        {
            SDL_Rect at = { x, (height - dot->h) / 2, 0, 0 };
            SDL_BlitSurface(dot, nullptr, tex.get(), &at);
            x += dot->w + padding / 2;
            SDL_FreeSurface(dot);
        }

        SDL_Surface* line = TTF_RenderUTF8_Blended(font.get(), text.c_str(), Color(0xdd, 0xdd, 0xdd));
        if (line)
        {
            SDL_Rect at = { x, (height - line->h) / 2, 0, 0 };
            SDL_BlitSurface(line, nullptr, tex.get(), &at);
            SDL_FreeSurface(line);
        }

        scene->UpdateTexture();
    }

public:
    StatusBarScene(std::shared_ptr<MoonrakerClient> client, int screenWidth, int screenHeight)
    : client(client),
      width(screenWidth),
      height(screenHeight / 18 < 28 ? 28 : screenHeight / 18),
      font(MakeFont("fonts/sazanami-gothic.ttf", height * 6 / 10)),
      // Power-of-two texture: GLES2 samples non-power-of-two textures with the
      // default REPEAT wrap mode as black. Only the top-left width x height is shown.
      tex(MakeSurface(NextPowerOfTwo(width), NextPowerOfTwo(height))),
      shader(new DefaultShader()),
      vbo(new QuadCollectionVbo()),
      scene(new VboScene(shader, vbo, tex))
    {
        if (!font)
        {
            printlog("StatusBarScene: cannot load font: %s\n", TTF_GetError());
        }
    }

    int GetHeight() const
    {
        return height;
    }

    virtual void Init(SDL_Window* window)
    {
        vbo->Add(QuadVbo(0, 0, 0, width, height, 0, 0, width, height, tex->w, tex->h, 0, 0, 1));
        scene->Init(window);
    }

    virtual void Update(const InputState& inputs, Uint32 timestamp)
    {
        scene->Update(inputs, timestamp);
    }

    virtual void Render(std::shared_ptr<RenderContextInterface> renderContext)
    {
        std::string text;
        SDL_Color dot;
        Describe(client->GetStatus(), text, dot);
        if (font && text != shownText)
        {
            Redraw(text, dot);
            shownText = text;
        }

        scene->SetMatrixTo2DView(renderContext->GetScreenWidth(), renderContext->GetScreenHeight());
        scene->Render(renderContext);
    }

    virtual bool Running() const
    {
        return true;
    }

    virtual void SetOrigin(Uint16 x, Uint16 y, Uint16 z)
    {
        scene->SetOrigin(x, y, z);
    }

    virtual Uint16 GetOriginX() const
    {
        return scene->GetOriginX();
    }

    virtual Uint16 GetOriginY() const
    {
        return scene->GetOriginY();
    }

    virtual Uint16 GetOriginZ() const
    {
        return scene->GetOriginZ();
    }
};

#endif // STATUSBARSCENE_HPP
