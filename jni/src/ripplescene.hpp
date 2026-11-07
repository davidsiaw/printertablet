#ifndef RIPPLESCENE_HPP
#define RIPPLESCENE_HPP

#include <cmath>
#include <cstdlib>
#include <vector>

#include "opengl.hpp"
#include "printlog.hpp"
#include "scene_interface.hpp"

// Screensaver: rings that spread out from random points and fade away.
// Nothing stays in one place, so nothing burns in.
class RippleScene : public SceneInterface
{
    struct Ripple
    {
        float x, y;
        Uint32 born;
    };

    static const int SEGMENTS = 96;          // straight pieces per circle
    static const int RINGS = 3;              // concentric rings per ripple
    static const Uint32 LIFETIME_MS = 5000;
    static const Uint32 MIN_GAP_MS = 500;    // between new ripples
    static const Uint32 MAX_GAP_MS = 1500;

    int width, height;
    float maxRadius, ringSpacing, ringWidth;

    std::vector<Ripple> ripples;
    Uint32 now, nextSpawn;
    std::vector<GLfloat> vertices;           // x, y, alpha

    GLuint program;
    GLint aPosition, aAlpha, uScreen, uColor;

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
            printlog("RippleScene: shader compile failed: %s\n", log);
        }
        return shader;
    }

    static float Random(float lo, float hi)
    {
        return lo + (hi - lo) * (rand() / (float)RAND_MAX);
    }

    void Spawn(Uint32 timestamp)
    {
        Ripple r = { Random(0, width), Random(0, height), timestamp };
        ripples.push_back(r);
        nextSpawn = timestamp + (Uint32)Random(MIN_GAP_MS, MAX_GAP_MS);
    }

    // One ring as a triangle strip of quads that are solid in the middle and
    // fade to nothing at both edges (inner edge -> middle -> outer edge).
    void AddRing(float cx, float cy, float radius, float alpha)
    {
        const float r[3] = { radius - ringWidth, radius, radius + ringWidth };
        const float a[3] = { 0, alpha, 0 };
        for (int half = 0; half < 2; half++)
        {
            vertices.clear();
            for (int i = 0; i <= SEGMENTS; i++)
            {
                float angle = i * 2.0f * (float)M_PI / SEGMENTS;
                float c = cosf(angle), s = sinf(angle);
                for (int k = half; k <= half + 1; k++)
                {
                    float rr = r[k] < 0 ? 0 : r[k];
                    vertices.push_back(cx + c * rr);
                    vertices.push_back(cy + s * rr);
                    vertices.push_back(a[k]);
                }
            }
            glVertexAttribPointer(aPosition, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), &vertices[0]);
            glVertexAttribPointer(aAlpha, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), &vertices[2]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, (SEGMENTS + 1) * 2);
        }
    }

public:
    RippleScene(int width, int height)
    : width(width), height(height),
      maxRadius(0.4f * (width > height ? width : height)),
      ringSpacing(height / 25.0f),
      ringWidth(height / 300.0f < 1.5f ? 1.5f : height / 300.0f),
      now(0), nextSpawn(0), program(0)
    {
    }

    ~RippleScene()
    {
        if (program) glDeleteProgram(program);
    }

    virtual void Init(SDL_Window* window)
    {
        const char* vertexShader = STRINGIFY(
            attribute vec2 aPosition;
            attribute float aAlpha;
            uniform vec2 uScreen;
            varying float vAlpha;
            void main()
            {
                // Pixels (origin top-left) to clip space.
                gl_Position = vec4(aPosition.x / uScreen.x * 2.0 - 1.0,
                                   1.0 - aPosition.y / uScreen.y * 2.0, 0.0, 1.0);
                vAlpha = aAlpha;
            }
        );
        const char* fragmentShader =
#ifdef HAVE_OPENGLES
            STRINGIFY(precision mediump float;)
#endif
            STRINGIFY(
            uniform vec4 uColor;
            varying float vAlpha;
            void main()
            {
                gl_FragColor = vec4(uColor.rgb, uColor.a * vAlpha);
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
        aAlpha = glGetAttribLocation(program, "aAlpha");
        uScreen = glGetUniformLocation(program, "uScreen");
        uColor = glGetUniformLocation(program, "uColor");
    }

    virtual void Update(const InputState& inputs, Uint32 timestamp)
    {
        // Coming back after a pause (or the first frame): start over rather
        // than dumping a burst of overdue ripples.
        if (timestamp - now > 1000)
        {
            ripples.clear();
            nextSpawn = timestamp;
        }
        now = timestamp;

        while (!ripples.empty() && now - ripples.front().born > LIFETIME_MS)
        {
            ripples.erase(ripples.begin());
        }
        if (now >= nextSpawn)
        {
            Spawn(now);
        }
    }

    virtual void Render(std::shared_ptr<RenderContextInterface> renderContext)
    {
        if (!program) return;

        glUseProgram(program);
        glBindBuffer(GL_ARRAY_BUFFER, 0);      // vertices come from client memory
        glUniform2f(uScreen, (GLfloat)width, (GLfloat)height);
        glUniform4f(uColor, 0.35f, 0.65f, 1.0f, 0.8f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);     // additive: overlapping rings glow
        glEnableVertexAttribArray(aPosition);
        glEnableVertexAttribArray(aAlpha);

        for (size_t i = 0; i < ripples.size(); i++)
        {
            float t = (now - ripples[i].born) / (float)LIFETIME_MS;   // 0..1
            float fade = (1 - t) * (1 - t);
            float lead = t * maxRadius;
            for (int ring = 0; ring < RINGS; ring++)
            {
                float radius = lead - ring * ringSpacing;
                if (radius <= 0) break;
                AddRing(ripples[i].x, ripples[i].y, radius, fade * (1.0f - ring / (float)RINGS));
            }
        }

        glDisableVertexAttribArray(aPosition);
        glDisableVertexAttribArray(aAlpha);
        glDisable(GL_BLEND);
    }

    virtual bool Running() const
    {
        return true;
    }
};

#endif // RIPPLESCENE_HPP
