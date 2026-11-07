#ifndef TOOLHEADPANELSCENE_HPP
#define TOOLHEADPANELSCENE_HPP

#include <cstdio>
#include <string>

#include "opengl.hpp"
#include "utils.hpp"
#include "defaultshader.hpp"
#include "quadcollectionvbo.hpp"
#include "vboscene.hpp"
#include "moonrakerclient.hpp"

// From top to bottom:
//   - three boxes showing X, Y and Z
//   - a `size` x `size` square: a map of the bed (10x10 grid) with a small
//     square where the toolhead is; or, while the printer is not homed (after
//     M84 or a restart), a big orange button that homes it with GANTRY
//   - while homed, a slider: drag it all the way right and let go to send M84
//
// Each part is a textured quad. Textures are only redrawn when what they show
// changes; the toolhead marker and the slider knob just move.
class ToolheadPanelScene : public ComposableSceneInterface
{
    static const int GRID = 10;

    std::shared_ptr<MoonrakerClient> client;
    int size, boxHeight, gap, sliderHeight, markerSize;
    Uint16 x, y, z;
    int screenW, screenH;

    std::shared_ptr<TTF_Font> font, bigFont;
    std::shared_ptr<ShaderInterface> shader;

    std::shared_ptr<SDL_Surface> bedTex, boxesTex, markerTex, trackTex, knobTex;
    std::shared_ptr<VboScene> bed, boxes, marker, track, knob;
    std::string shownBoxes;   // what the boxes texture currently says
    std::string shownBed;     // "grid", or the button's label

    // Touch
    bool wasDown;
    bool pressingButton;
    bool dragging;
    float dragOffset;         // finger x minus knob x when the drag started
    int knobPos;              // knob offset from the left of the track

    static int NextPowerOfTwo(int n)
    {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    static SDL_Color Color(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 0xff)
    {
        SDL_Color c;
        c.r = r; c.g = g; c.b = b; c.a = a;
        return c;
    }

    static SDL_Color Grey() { return Color(0x48, 0x48, 0x48); }
    static SDL_Color Orange() { return Color(0xff, 0x8c, 0x1a); }

    static void Fill(SDL_Surface* s, int x, int y, int w, int h, SDL_Color c)
    {
        SDL_Rect r = { x, y, w, h };
        SDL_FillRect(s, &r, SDL_MapRGBA(s->format, c.r, c.g, c.b, c.a));
    }

    // Draws text centred in the rectangle.
    static void Text(SDL_Surface* s, TTF_Font* font, const std::string& text, int x, int y, int w, int h, SDL_Color c)
    {
        if (!font) return;
        SDL_Surface* rendered = TTF_RenderUTF8_Blended(font, text.c_str(), c);
        if (rendered)
        {
            SDL_Rect at = { x + (w - rendered->w) / 2, y + (h - rendered->h) / 2, 0, 0 };
            SDL_BlitSurface(rendered, nullptr, s, &at);
            SDL_FreeSurface(rendered);
        }
    }

    // A quad showing the top-left w x h of a power-of-two texture.
    std::shared_ptr<VboScene> MakeQuad(std::shared_ptr<SDL_Surface>& tex, int w, int h)
    {
        // Power-of-two textures: GLES2 samples non-power-of-two textures with
        // the default REPEAT wrap mode as black.
        tex = MakeSurface(NextPowerOfTwo(w), NextPowerOfTwo(h));
        std::shared_ptr<QuadCollectionVbo> vbo(new QuadCollectionVbo());
        vbo->Add(QuadVbo(0, 0, 0, w, h, 0, 0, w, h, tex->w, tex->h, 0, 0, 1));
        return std::shared_ptr<VboScene>(new VboScene(shader, vbo, tex));
    }

    void DrawBed(const std::string& what)
    {
        SDL_Surface* s = bedTex.get();
        if (what == "grid")
        {
            Fill(s, 0, 0, size, size, Grey());
            int line = size / 300 < 1 ? 1 : size / 300;
            SDL_Color gridColor = Color(0x70, 0x70, 0x70);
            for (int i = 0; i <= GRID; i++)
            {
                int at = i * (size - line) / GRID;
                Fill(s, at, 0, line, size, gridColor);   // vertical
                Fill(s, 0, at, size, line, gridColor);   // horizontal
            }
        }
        else
        {
            Fill(s, 0, 0, size, size, Orange());
            Text(s, bigFont.get(), what, 0, 0, size, size, Color(0xff, 0xff, 0xff));
        }
        bed->UpdateTexture();
    }

    void DrawBoxes(const std::string labels[3])
    {
        SDL_Surface* s = boxesTex.get();
        Fill(s, 0, 0, s->w, s->h, Color(0, 0, 0, 0));   // gaps between boxes show through
        int boxWidth = (size - 2 * gap) / 3;
        for (int i = 0; i < 3; i++)
        {
            int bx = i * (boxWidth + gap);
            Fill(s, bx, 0, boxWidth, boxHeight, Grey());
            Text(s, font.get(), labels[i], bx, 0, boxWidth, boxHeight, Color(0xdd, 0xdd, 0xdd));
        }
        boxes->UpdateTexture();
    }

    static std::string Label(const char* axis, bool known, double value)
    {
        char buf[32];
        if (known)
        {
            snprintf(buf, sizeof(buf), "%s %.2f", axis, value);
        }
        else
        {
            snprintf(buf, sizeof(buf), "%s  -", axis);
        }
        return buf;
    }

    static bool IsHomed(const MoonrakerClient::Status& status)
    {
        const std::string& h = status.homedAxes;
        return status.hasToolhead &&
               h.find('x') != std::string::npos &&
               h.find('y') != std::string::npos &&
               h.find('z') != std::string::npos;
    }

    int BedTop() const { return y + boxHeight + gap; }
    int TrackTop() const { return BedTop() + size + gap; }
    int KnobTravel() const { return size - sliderHeight; }

    static bool Inside(float px, float py, int x, int y, int w, int h)
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }

public:
    ToolheadPanelScene(std::shared_ptr<MoonrakerClient> client, int size, int boxHeight, int gap, int sliderHeight)
    : client(client), size(size), boxHeight(boxHeight), gap(gap), sliderHeight(sliderHeight),
      markerSize(size / 25 < 6 ? 6 : size / 25),
      x(0), y(0), z(0), screenW(0), screenH(0),
      font(MakeFont("fonts/sazanami-gothic.ttf", boxHeight * 4 / 10)),
      bigFont(MakeFont("fonts/sazanami-gothic.ttf", size / 7)),
      shader(new DefaultShader()),
      wasDown(false), pressingButton(false), dragging(false), dragOffset(0), knobPos(0)
    {
        if (!font || !bigFont)
        {
            printlog("ToolheadPanelScene: cannot load font: %s\n", TTF_GetError());
        }
        bed = MakeQuad(bedTex, size, size);
        boxes = MakeQuad(boxesTex, size, boxHeight);
        marker = MakeQuad(markerTex, markerSize, markerSize);
        track = MakeQuad(trackTex, size, sliderHeight);
        knob = MakeQuad(knobTex, sliderHeight, sliderHeight);
    }

    int GetHeight() const
    {
        return boxHeight + gap + size + gap + sliderHeight;
    }

    virtual void Init(SDL_Window* window)
    {
        Fill(markerTex.get(), 0, 0, markerSize, markerSize, Orange());

        Fill(trackTex.get(), 0, 0, size, sliderHeight, Grey());
        Text(trackTex.get(), font.get(), "slide to turn off motors", sliderHeight, 0, size - sliderHeight, sliderHeight,
             Color(0x99, 0x99, 0x99));

        Fill(knobTex.get(), 0, 0, sliderHeight, sliderHeight, Orange());
        Text(knobTex.get(), font.get(), "\xC2\xBB", 0, 0, sliderHeight, sliderHeight, Color(0xff, 0xff, 0xff));   // »

        bed->Init(window);
        boxes->Init(window);
        marker->Init(window);
        track->Init(window);
        knob->Init(window);
    }

    virtual void Update(const InputState& inputs, Uint32 timestamp)
    {
        if (screenW == 0) return;   // no Render yet, so no screen size

        MoonrakerClient::Status status = client->GetStatus();
        bool homed = IsHomed(status);
        bool showButton = status.hasToolhead && !homed;
        bool busy = client->IsCommandRunning();

        bool down = inputs.GetFingered();
        float px = inputs.GetFingerX() * screenW;   // SDL gives 0..1
        float py = inputs.GetFingerY() * screenH;

        if (down && !wasDown)
        {
            if (showButton && !busy && Inside(px, py, x, BedTop(), size, size))
            {
                pressingButton = true;
            }
            else if (homed && !busy && Inside(px, py, x + knobPos, TrackTop(), sliderHeight, sliderHeight))
            {
                dragging = true;
                dragOffset = px - (x + knobPos);
            }
        }

        if (down && dragging)
        {
            int pos = (int)(px - dragOffset) - x;
            knobPos = pos < 0 ? 0 : pos > KnobTravel() ? KnobTravel() : pos;
        }

        if (!down && wasDown)
        {
            // A tap only counts if it is let go on the button too.
            if (pressingButton && showButton && Inside(px, py, x, BedTop(), size, size))
            {
                client->SendGcode("GANTRY");
            }
            if (dragging && homed && knobPos >= KnobTravel())
            {
                client->SendGcode("M84");
            }
            pressingButton = false;
            dragging = false;
            knobPos = 0;   // springs back
        }

        wasDown = down;
    }

    virtual void Render(std::shared_ptr<RenderContextInterface> renderContext)
    {
        screenW = renderContext->GetScreenWidth();
        screenH = renderContext->GetScreenHeight();

        MoonrakerClient::Status status = client->GetStatus();
        bool known = status.hasToolhead;
        bool homed = IsHomed(status);

        std::string labels[3] = {
            Label("X", known, status.position[0]),
            Label("Y", known, status.position[1]),
            Label("Z", known, status.position[2]),
        };
        std::string text = labels[0] + labels[1] + labels[2];
        if (text != shownBoxes)
        {
            DrawBoxes(labels);
            shownBoxes = text;
        }

        std::string bedWhat = "grid";
        if (known && !homed)
        {
            bedWhat = client->IsCommandRunning() ? "HOMING..." : "HOME";
        }
        if (bedWhat != shownBed)
        {
            DrawBed(bedWhat);
            shownBed = bedWhat;
        }

        boxes->SetMatrixTo2DView(screenW, screenH);
        boxes->SetOrigin(x, y, z);
        boxes->Render(renderContext);

        bed->SetMatrixTo2DView(screenW, screenH);
        bed->SetOrigin(x, BedTop(), z);
        bed->Render(renderContext);

        if (!homed)
        {
            return;   // no meaningful position to show, and nothing to turn off
        }

        double spanX = status.axisMax[0] - status.axisMin[0];
        double spanY = status.axisMax[1] - status.axisMin[1];
        if (spanX > 0 && spanY > 0)
        {
            // Fraction across the bed; Y grows towards the back, i.e. up the screen.
            double fx = (status.position[0] - status.axisMin[0]) / spanX;
            double fy = (status.position[1] - status.axisMin[1]) / spanY;
            fx = fx < 0 ? 0 : fx > 1 ? 1 : fx;
            fy = fy < 0 ? 0 : fy > 1 ? 1 : fy;
            int mx = x + (int)(fx * (size - markerSize));
            int my = BedTop() + (int)((1 - fy) * (size - markerSize));

            marker->SetMatrixTo2DView(screenW, screenH);
            marker->SetOrigin(mx, my, z);
            marker->Render(renderContext);
        }

        track->SetMatrixTo2DView(screenW, screenH);
        track->SetOrigin(x, TrackTop(), z);
        track->Render(renderContext);

        knob->SetMatrixTo2DView(screenW, screenH);
        knob->SetOrigin(x + knobPos, TrackTop(), z);
        knob->Render(renderContext);
    }

    virtual bool Running() const
    {
        return true;
    }

    virtual void SetOrigin(Uint16 x, Uint16 y, Uint16 z)
    {
        this->x = x;
        this->y = y;
        this->z = z;
    }

    virtual Uint16 GetOriginX() const { return x; }
    virtual Uint16 GetOriginY() const { return y; }
    virtual Uint16 GetOriginZ() const { return z; }
};

#endif // TOOLHEADPANELSCENE_HPP
