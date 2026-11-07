#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>

#include <memory>
#include <functional>
#include <list>

#include <Box2D/Box2D.h>

#include "opengl.hpp"

#include "scene_interface.hpp"
#include "inputstate.hpp"

#include "printlog.hpp"

#include "inputmachine.hpp"

#include "manifestbasedresources.hpp"

#include "notifier.hpp"

#include "emptyscene.hpp"
#include "guiscene.hpp"
#include "vboscene.hpp"
#include "fpsscene.hpp"

#include "utils.hpp"

#include "defaultshader.hpp"
#include "wireframeshader.hpp"
#include "colorshader.hpp"
#include "trianglevbo.hpp"
#include "quadvbo.hpp"
#include "quadcollectionvbo.hpp"
#include "viewscene.hpp"
#include "compositescene.hpp"
#include "gamecontext.hpp"

#include "twodarray.hpp"

#include "rpgmapscene.hpp"

#include "moonrakerclient.hpp"
#include "statusbarscene.hpp"
#include "toolheadpanelscene.hpp"
#include "ripplescene.hpp"
#include "screensaver.hpp"
#include "scripting.hpp"
#include <mruby.hpp>

// The printer's Moonraker server. Defaults; main.rb sets the real one with
// `moonraker "host", port` (Moonraker's default port is 7125).
static std::string moonrakerHost = "192.168.1.100";
static int moonrakerPort = 7125;

static void SetMoonraker(std::string host, int port)
{
    moonrakerHost = host;
    moonrakerPort = port;
}


SDL_Surface* flipVert(SDL_Surface* sfc)
{
    SDL_Surface* result = SDL_CreateRGBSurface(sfc->flags, sfc->w, sfc->h,
                                               sfc->format->BytesPerPixel * 8, sfc->format->Rmask, sfc->format->Gmask,
                                               sfc->format->Bmask, sfc->format->Amask);
    if (result == NULL) return NULL;
    
    Uint8* pixels = (Uint8*) sfc->pixels;
    Uint8* rpixels = (Uint8*) result->pixels;
    
    Uint32 pitch = sfc->pitch;
    Uint32 pxlength = pitch*sfc->h;
    
    for(int line = 0; line < sfc->h; ++line) {
        Uint32 pos = line * pitch;
        memcpy(&rpixels[pos], &pixels[(pxlength-pos)-pitch], pitch);
    }
    
    return result;
}

void takeScreenShot(std::string filename, SDL_Window* window)
{
    int windowWidth, windowHeight;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
	SDL_Surface* surf = SDL_CreateRGBSurface(
		SDL_SWSURFACE, 
		windowWidth,
		windowHeight,
		24, 
		0x000000FF, 
		0x0000FF00, 
		0x00FF0000, 
		0);
    
	if (surf == NULL) return;

	glReadPixels(0, 0, windowWidth, windowHeight, GL_RGB, GL_UNSIGNED_BYTE, surf->pixels);

    SDL_Surface * flip = flipVert(surf);
    if (flip == NULL) return;
    SDL_FreeSurface(surf);
    
    SDL_SaveBMP(flip, filename.c_str());
    
    SDL_FreeSurface(flip);
}

int exists(const char *fname)
{
    FILE *file;
    if ((file = fopen(fname, "r")))
    {
        fclose(file);
        return 1;
    }
    return 0;
}

// Builds the scene graph. Called again whenever the GL context is replaced,
// because every shader, texture and buffer belongs to the old context.
typedef std::function<std::shared_ptr<SceneInterface>()> SceneFactory;

static void DrainGLErrors()
{
	while (glGetError() != GL_NO_ERROR) { }
}

void run(SceneFactory makeScene, std::shared_ptr<GameContext> context)
{
	InputState inputState;
	Uint32 time = SDL_GetTicks();
	std::shared_ptr<SceneInterface> scene = makeScene();
	scene->Init(context->GetWindow());

	// In the background there is no surface to draw on (Android destroys it
	// when you press Home), so stop rendering until we are back.
	bool inBackground = false;
	SDL_GLContext glContext = SDL_GL_GetCurrentContext();
	
	while (scene->Running())
	{
		SDL_Event e;
		if ( SDL_PollEvent(&e) )
		{
			if (e.type == SDL_QUIT)
			{
				break;
			}
			if (e.type == SDL_APP_WILLENTERBACKGROUND || e.type == SDL_APP_DIDENTERBACKGROUND)
			{
				inBackground = true;
			}
			if (e.type == SDL_APP_DIDENTERFOREGROUND)
			{
				inBackground = false;
			}
            if (e.type == SDL_KEYDOWN)
            {
                if (e.key.keysym.sym == SDLK_1 && (e.key.keysym.mod & (KMOD_CTRL | KMOD_SHIFT)) != 0)
                {
                    int count = 0;
                    std::string fname = "screenshot";
                    do
                    {
                        std::stringstream ss;
                        ss << "screenshot" << count << ".bmp";
                        fname = ss.str();
                        count++;
                    }
                    while (exists(fname.c_str()));
                    takeScreenShot(fname, context->GetWindow());
                }
            }
			CaptureInputState(context->GetKeyMap(), &inputState, &e);
		}
		
		if (inBackground)
		{
			continue;   // SDL blocks inside SDL_PollEvent until we are resumed
		}

		// On resume SDL may have had to create a brand new GL context (the old
		// one was lost); our GL objects are gone, so rebuild everything.
		if (SDL_GL_GetCurrentContext() != glContext)
		{
			printlog("GL context was replaced on resume; rebuilding the scene\n");
			scene.reset();      // free old objects before the new ones get names
			DrainGLErrors();    // deleting objects of a dead context sets errors
			glContext = SDL_GL_GetCurrentContext();
			glViewport(0, 0, context->GetScreenWidth(), context->GetScreenHeight());
			scene = makeScene();
			scene->Init(context->GetWindow());
		}

		Uint32 now = SDL_GetTicks();
		scene->Update(inputState, now);
		if (now - time > 15)
        {
            glClearColor( 0.0, 0.0, 0.0, 1.0 );
            glClear ( GL_COLOR_BUFFER_BIT );
            scene->Render(context);
            SDL_GL_SwapWindow(context->GetWindow());
			time = SDL_GetTicks();
		}
	}
}






class ProcessingScene : public ComposableSceneInterface
{
    std::function<void(const InputState&, Uint32)> update;
    std::function<bool()> running;
    
public:
    
    ProcessingScene(std::function<void(const InputState&, Uint32)> update, std::function<bool()> running) :
    update(update), running(running)
    {
        
    }
    
    virtual void Init(SDL_Window* window) { }

    virtual void Update(const InputState& inputs, Uint32 timestamp)
    {
        update(inputs, timestamp);
    }

    virtual void Render(std::shared_ptr<RenderContextInterface> renderContext) { }

    virtual bool Running() const
    {
        return running();
    }
    
    virtual void SetOrigin(Uint16 x, Uint16 y, Uint16 z) { }
    virtual Uint16 GetOriginX() const { return 0; }
    virtual Uint16 GetOriginY() const { return 0; }
    virtual Uint16 GetOriginZ() const { return 0; }

};

void game(std::shared_ptr<GameContext> context)
{
    Scripting scripting;
    scripting.GetVM().bind_method("moonraker", SetMoonraker);
    scripting.RunFile(Scripting::MainScriptPath());

    auto moonraker = std::make_shared<MoonrakerClient>(moonrakerHost, moonrakerPort);

    SceneFactory makeScene = [&]() -> std::shared_ptr<SceneInterface>
    {
        auto wholeScene = std::make_shared<CompositeScene>();

        auto statusBar = std::make_shared<StatusBarScene>(moonraker, context->GetScreenWidth(), context->GetScreenHeight());
        wholeScene->AddScene(statusBar, 0, context->GetScreenHeight() - statusBar->GetHeight());

        // Toolhead panel on the left, filling the height above the status bar.
        int margin = context->GetScreenHeight() / 30;
        int gap = margin / 2;
        int available = context->GetScreenHeight() - statusBar->GetHeight() - 2 * margin;
        int boxHeight = available / 9;
        int sliderHeight = boxHeight;
        int size = available - boxHeight - sliderHeight - 2 * gap;
        auto toolhead = std::make_shared<ToolheadPanelScene>(moonraker, size, boxHeight, gap, sliderHeight);
        wholeScene->AddScene(toolhead, margin, margin);

        auto ripples = std::make_shared<RippleScene>(context->GetScreenWidth(), context->GetScreenHeight());
        return std::make_shared<ScreensaverSwitch>(wholeScene, ripples);
    };

    run(makeScene, context);
}

int main(int argc, char** argv)
{
    std::map<int, InputState::Key> keyMap;

	keyMap[SDLK_UP] = InputState::UP;
	keyMap[SDLK_DOWN] = InputState::DOWN;
	keyMap[SDLK_LEFT] = InputState::LEFT;
	keyMap[SDLK_RIGHT] = InputState::RIGHT;
	keyMap[SDLK_a] = InputState::A;
	keyMap[SDLK_s] = InputState::B;
	keyMap[SDLK_z] = InputState::X;
	keyMap[SDLK_x] = InputState::Y;
	keyMap[SDLK_1] = InputState::L1;
	keyMap[SDLK_3] = InputState::L2;
	keyMap[SDLK_2] = InputState::R1;
	keyMap[SDLK_4] = InputState::R2;
	keyMap[SDLK_SPACE] = InputState::START;
    
	SDL_Init(SDL_INIT_EVERYTHING);
	TTF_Init();
	IMG_Init(IMG_INIT_JPG|IMG_INIT_PNG);
    
	// Something to note here: We don't initialize mp3 because there are too many
	// issues with it. Instead, simply make FLAC or OGG files. They are free
	// FLAC is better too because of its higher quality.
	Mix_Init(MIX_INIT_FLAC|MIX_INIT_MOD|MIX_INIT_OGG);
    

	int WIDTH = 960, HEIGHT = 640;
    
    if (argc == 2)
    {
        printlog("Configuration file provided: %s\n", argv[1]);
        LuaTable config = LoadLuaConfiguration(argv[1]);
    }
    
	//if (SDL_GetCurrentDisplayMode(0, &mode)==0)
	//{
	//	/* I read that android ignores these so you can just as well set
	//	them to 0 */
	//	WIDTH=mode.w;
	//	HEIGHT=mode.h;
	//}
    
	if(Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024)==-1)
	{
		printlog("Mix_OpenAudio: %s\n", Mix_GetError());
	}
    
	printlog("Window size: %d x %d!\n", WIDTH, HEIGHT);
    
	// Create an application window with the following settings:
	SDL_Window* window = SDL_CreateWindow(
                              "Printer Tablet",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              WIDTH,
                              HEIGHT,
                              SDL_WINDOW_SHOWN|SDL_WINDOW_OPENGL
                              );
    
	// Check that the window was successfully made
	if(window == NULL)
	{
		// In the event that the window could not be made...
		printlog("Could not create window: %s\n", SDL_GetError());
		return 1;
	}
	
	// Mobile platforms ignore the requested size and go fullscreen; use what we got.
	SDL_GetWindowSize(window, &WIDTH, &HEIGHT);
	printlog("Actual window size: %d x %d\n", WIDTH, HEIGHT);

	printlog("Beginning game with window=%p\n", window);
	try
	{
        /* Request opengl 3.2 context.
         * SDL doesn't have the ability to choose which profile at this time of writing,
         * but it should default to the core profile */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        
        
        SDL_GLContext context = SDL_GL_CreateContext(window);
        
        
        printlog("OpenGL context created\n");
        
        glViewport(0, 0, WIDTH, HEIGHT);
        
        std::shared_ptr<GameContext> gameContext = std::make_shared<GameContext>(window, keyMap, WIDTH, HEIGHT);
        
		game(gameContext);
        
		SDL_GL_DeleteContext(context);
	}
	catch(std::exception e)
	{
		printlog("Caught a std::exception! %s\n", e.what());
        return EXIT_FAILURE;
	}
	catch(...)
	{
		printlog("Caught an unknown exception!\n");
        return EXIT_FAILURE;
	}
    
	SDL_DestroyWindow(window);
    
	// Clean up
	Mix_CloseAudio();
	Mix_Quit();
    
	TTF_Quit();
    
	SDL_Quit(); 
	
	printlog("Program Successfully Terminated!\n");
	
	return EXIT_SUCCESS;
}
