/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#include <Application.h>

#include "CompositorWindow.h"


class CompositorApp : public BApplication {
public:
	CompositorApp()
		:	BApplication("application/x-vnd.Haiku-Compositor")
	{
	}

	void ReadyToRun() override
	{
		CompositorWindow* window = new CompositorWindow();
		window->Show();
	}
};


int
main()
{
	CompositorApp app;
	app.Run();
	return 0;
}
