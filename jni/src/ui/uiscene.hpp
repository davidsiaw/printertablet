#ifndef UISCENE_HPP
#define UISCENE_HPP

#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <SDL.h>
#include <SDL_ttf.h>

#include "opengl.hpp"
#include "printlog.hpp"
#include "composablescene_interface.hpp"
#include "uitree.hpp"

// Draws the UiTree every frame, and turns finger input into touch events for
// the node under the finger. All GL objects live here, so after the GL context
// is replaced a new UiScene simply re-renders the (unchanged) tree.
class UiScene : public ComposableSceneInterface
{
public:
    enum TouchKind { TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP };
    // Ruby handles these: Native::UI.touch, in lib/ui.rb.
    typedef std::function<void(int id, TouchKind kind, double x, double y)> TouchHandler;

private:
    UiTree& tree;
    int width, height;
    TouchHandler onTouch;

    GLuint program;
    GLint aPosition, aTexCoord, uScreen, uColor, uUseTexture, uTexture;

    struct TextTexture
    {
        GLuint texture;
        std::string key;          // text + font size it was rendered from
        int w, h;                 // text size in pixels
        float u, v;               // how much of the (power-of-two) texture it fills
        bool used;                // seen this frame; unused ones are freed
    };
    std::map<int, TextTexture> texts;          // by node id
    std::map<int, TTF_Font*> fonts;            // by point size

    // Touch
    bool wasDown;
    double lastX, lastY;
    int captured;                 // node that got the touch-down; gets move/up too

    static GLuint Compile(GLenum type, const char* source)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, NULL);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[512];
            glGetShaderInfoLog(shader, sizeof(log), NULL, log);
            printlog("UiScene: shader compile failed: %s\n", log);
        }
        return shader;
    }

    static int NextPowerOfTwo(int n)
    {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    TTF_Font* Font(int size)
    {
        std::map<int, TTF_Font*>::iterator it = fonts.find(size);
        if (it != fonts.end()) return it->second;
        TTF_Font* font = TTF_OpenFont("fonts/sazanami-gothic.ttf", size);
        if (!font) printlog("UiScene: cannot load font: %s\n", TTF_GetError());
        fonts[size] = font;
        return font;
    }

    // The texture for a text node, (re)rendered if its text, size or width
    // changed. Text wider than the node (minus padding) is rendered smaller,
    // so it always fits.
    TextTexture* Text(int id, const UiTree::Node& n)
    {
        int maxWidth = (int)(n.w - 2 * n.padding);
        char size[32];
        snprintf(size, sizeof(size), "%d|%d|", n.fontSize, maxWidth);
        std::string key = size + n.text;

        TextTexture& t = texts[id];   // new entries start zeroed
        t.used = true;
        if (t.texture && t.key == key) return t.w ? &t : nullptr;

        if (!t.texture) glGenTextures(1, &t.texture);
        t.key = key;
        t.w = t.h = 0;

        TTF_Font* font = Font(n.fontSize);
        if (!font || n.text.empty()) return nullptr;

        // Too wide? Measure, then use a size that fits (font widths scale
        // roughly with the size; shrink a little more until it does).
        int textW = 0, textH = 0;
        if (maxWidth > 0 && TTF_SizeUTF8(font, n.text.c_str(), &textW, &textH) == 0 && textW > maxWidth)
        {
            int fitted = n.fontSize * maxWidth / textW;
            for (; fitted > 4; fitted--)
            {
                font = Font(fitted);
                if (!font) return nullptr;
                if (TTF_SizeUTF8(font, n.text.c_str(), &textW, &textH) == 0 && textW <= maxWidth) break;
            }
        }

        // White text; the node's colour is applied when drawing.
        SDL_Color white = { 0xff, 0xff, 0xff, 0xff };
        SDL_Surface* rendered = TTF_RenderUTF8_Blended(font, n.text.c_str(), white);
        if (!rendered) return nullptr;

        // Power-of-two texture: GLES2 samples non-power-of-two textures with
        // the default REPEAT wrap mode as black.
        int tw = NextPowerOfTwo(rendered->w), th = NextPowerOfTwo(rendered->h);
        SDL_Surface* rgba = SDL_CreateRGBSurface(0, tw, th, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
        if (rgba)
        {
            SDL_FillRect(rgba, nullptr, 0);
            SDL_SetSurfaceBlendMode(rendered, SDL_BLENDMODE_NONE);   // copy alpha as-is
            SDL_BlitSurface(rendered, nullptr, rgba, nullptr);

            glBindTexture(GL_TEXTURE_2D, t.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);

            t.w = rendered->w;
            t.h = rendered->h;
            t.u = rendered->w / (float)tw;
            t.v = rendered->h / (float)th;
            SDL_FreeSurface(rgba);
        }
        SDL_FreeSurface(rendered);
        return t.w ? &t : nullptr;
    }

    void Quad(double x, double y, double w, double h, Uint32 rgb, float alpha, float u = 0, float v = 0, GLuint texture = 0)
    {
        GLfloat pos[8] = { (GLfloat)x, (GLfloat)y,   (GLfloat)(x + w), (GLfloat)y,
                           (GLfloat)x, (GLfloat)(y + h), (GLfloat)(x + w), (GLfloat)(y + h) };
        GLfloat uv[8] = { 0, 0,  u, 0,  0, v,  u, v };
        glUniform4f(uColor, ((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, alpha);
        glUniform1f(uUseTexture, texture ? 1.0f : 0.0f);
        if (texture) glBindTexture(GL_TEXTURE_2D, texture);
        glVertexAttribPointer(aPosition, 2, GL_FLOAT, GL_FALSE, 0, pos);
        glVertexAttribPointer(aTexCoord, 2, GL_FLOAT, GL_FALSE, 0, uv);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    void Draw(int id, double ox, double oy)
    {
        UiTree::Node* n = tree.Get(id);
        if (!n || !n->visible) return;
        double x = ox + n->x, y = oy + n->y;

        if (n->kind != UiTree::GROUP && n->hasColor)
        {
            Quad(x, y, n->w, n->h, n->color, n->alpha);
        }
        if (n->kind == UiTree::TEXT)
        {
            TextTexture* t = Text(id, *n);
            if (t)
            {
                double tx = x + (n->w - t->w) / 2;
                if (n->align == UiTree::LEFT) tx = x + n->padding;
                if (n->align == UiTree::RIGHT) tx = x + n->w - n->padding - t->w;
                double ty = y + (n->h - t->h) / 2;
                Quad((int)tx, (int)ty, t->w, t->h, n->textColor, n->alpha, t->u, t->v, t->texture);
            }
        }

        // Copy: Text() can't change the tree, but be safe against reentrancy.
        std::vector<int> children = n->children;
        for (size_t i = 0; i < children.size(); i++)
        {
            Draw(children[i], x, y);
        }
    }

public:
    UiScene(UiTree& tree, int width, int height, TouchHandler onTouch)
    : tree(tree), width(width), height(height), onTouch(onTouch), program(0),
      wasDown(false), lastX(0), lastY(0), captured(0)
    {
    }

    ~UiScene()
    {
        for (std::map<int, TextTexture>::iterator it = texts.begin(); it != texts.end(); ++it)
        {
            if (it->second.texture) glDeleteTextures(1, &it->second.texture);
        }
        for (std::map<int, TTF_Font*>::iterator it = fonts.begin(); it != fonts.end(); ++it)
        {
            if (it->second) TTF_CloseFont(it->second);
        }
        if (program) glDeleteProgram(program);
    }

    virtual void Init(SDL_Window* window)
    {
        const char* vertexShader = STRINGIFY(
            attribute vec2 aPosition;
            attribute vec2 aTexCoord;
            uniform vec2 uScreen;
            varying vec2 vTexCoord;
            void main()
            {
                // Pixels (origin top-left) to clip space.
                gl_Position = vec4(aPosition.x / uScreen.x * 2.0 - 1.0,
                                   1.0 - aPosition.y / uScreen.y * 2.0, 0.0, 1.0);
                vTexCoord = aTexCoord;
            }
        );
        const char* fragmentShader =
#ifdef HAVE_OPENGLES
            STRINGIFY(precision mediump float;)
#endif
            STRINGIFY(
            uniform vec4 uColor;
            uniform float uUseTexture;
            uniform sampler2D uTexture;
            varying vec2 vTexCoord;
            void main()
            {
                vec4 texel = texture2D(uTexture, vTexCoord);
                gl_FragColor = mix(uColor, uColor * texel, uUseTexture);
            }
        );

        GLuint vs = Compile(GL_VERTEX_SHADER, vertexShader);
        GLuint fs = Compile(GL_FRAGMENT_SHADER, fragmentShader);
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);

        aPosition = glGetAttribLocation(program, "aPosition");
        aTexCoord = glGetAttribLocation(program, "aTexCoord");
        uScreen = glGetUniformLocation(program, "uScreen");
        uColor = glGetUniformLocation(program, "uColor");
        uUseTexture = glGetUniformLocation(program, "uUseTexture");
        uTexture = glGetUniformLocation(program, "uTexture");
    }

    virtual void Update(const InputState& inputs, Uint32 timestamp)
    {
        bool down = inputs.GetFingered();
        double x = inputs.GetFingerX() * width;    // SDL gives 0..1
        double y = inputs.GetFingerY() * height;

        if (down && !wasDown)
        {
            captured = tree.HitTest(x, y);
            if (captured) onTouch(captured, TOUCH_DOWN, x, y);
        }
        else if (down && captured && (x != lastX || y != lastY))
        {
            onTouch(captured, TOUCH_MOVE, x, y);
        }
        else if (!down && wasDown && captured)
        {
            // The finger-up event carries no position: use the last one.
            int id = captured;
            captured = 0;
            onTouch(id, TOUCH_UP, lastX, lastY);
        }

        wasDown = down;
        if (down)
        {
            lastX = x;
            lastY = y;
        }
    }

    virtual void Render(std::shared_ptr<RenderContextInterface> renderContext)
    {
        if (!program || !tree.GetRoot()) return;

        glUseProgram(program);
        glBindBuffer(GL_ARRAY_BUFFER, 0);      // vertices come from client memory
        glUniform2f(uScreen, (GLfloat)width, (GLfloat)height);
        glUniform1i(uTexture, 0);
        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnableVertexAttribArray(aPosition);
        glEnableVertexAttribArray(aTexCoord);

        for (std::map<int, TextTexture>::iterator it = texts.begin(); it != texts.end(); ++it)
        {
            it->second.used = false;
        }

        Draw(tree.GetRoot(), 0, 0);

        // Free textures of text nodes that are gone or no longer shown.
        for (std::map<int, TextTexture>::iterator it = texts.begin(); it != texts.end(); )
        {
            if (!it->second.used)
            {
                if (it->second.texture) glDeleteTextures(1, &it->second.texture);
                texts.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        glDisableVertexAttribArray(aPosition);
        glDisableVertexAttribArray(aTexCoord);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_BLEND);
    }

    virtual bool Running() const
    {
        return true;
    }

    // Always the whole screen; node positions are screen positions.
    virtual void SetOrigin(Uint16, Uint16, Uint16) { }
    virtual Uint16 GetOriginX() const { return 0; }
    virtual Uint16 GetOriginY() const { return 0; }
    virtual Uint16 GetOriginZ() const { return 0; }
};

#endif // UISCENE_HPP
