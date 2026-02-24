/*
 * Copyright 2025, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#include <Application.h>
#include <ExperimentalCompositorKit.h>
#include <String.h>

#include <cstdio>

class ExperimentalCompositorApp : public BApplication {
public:
	ExperimentalCompositorApp()
		:	BApplication("application/x-vnd.haiku-experimental-compositor-demo")
	{
	}

	void ReadyToRun() override
	{
		status_t status = fCompositor.Connect();
		if (status != B_OK) {
			printf("Failed to connect: %s\n", fCompositor.LastError().String());
			PostMessage(B_QUIT_REQUESTED);
			return;
		}

		status = fCompositor.SetEffect("scanlines");
		if (status != B_OK)
			printf("Failed to set effect: %s\n", fCompositor.LastError().String());

		const uint8 texture[] = {
			0xff, 0x00, 0x00, 0xff,
			0x00, 0xff, 0x00, 0xff,
			0x00, 0x00, 0xff, 0xff,
			0xff, 0xff, 0x00, 0xff
		};
		status = fCompositor.SetTexture(texture, sizeof(texture), 2, 2);
		if (status != B_OK)
			printf("Failed to set texture: %s\n", fCompositor.LastError().String());

		fCompositor.Disconnect();
		PostMessage(B_QUIT_REQUESTED);
	}

private:
	BExperimentalCompositor fCompositor;
};

int
main()
{
	ExperimentalCompositorApp app;
	app.Run();
	return 0;
}
