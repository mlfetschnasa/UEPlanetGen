# Project & Module Setup

## 1. Folder structure

Drop the `PlanetGen` folder into your project's `Source/` directory so it looks like this:

```
YourProject/
  YourProject.uproject
  Source/
    YourProject/                      <- your existing game module, untouched
      YourProject.Build.cs
      ...
    PlanetGen/                        <- this module
      PlanetGen.Build.cs
      Public/
        PlanetMath.h
        NoiseGenerator.h
        PlanetQuadtree.h
        PlanetChunk.h
        PlanetChunkPool.h
        PlanetManager.h
        PlanetOceanShell.h
        PlanetGlowShell.h
        PlanetAtmosphereSettings.h
        PlanetAtmosphereController.h
        PlanetSystemManager.h
      Private/
        PlanetMath.cpp
        NoiseGenerator.cpp
        PlanetQuadtree.cpp
        PlanetChunk.cpp
        PlanetChunkPool.cpp
        PlanetManager.cpp
        PlanetOceanShell.cpp
        PlanetGlowShell.cpp
        PlanetAtmosphereController.cpp
        PlanetSystemManager.cpp
  Content/
    Planet/                           <- create this, see Docs/MaterialSetup.md
```

## 2. Register the module

Open `YourProject.uproject` and add `PlanetGen` to the `Modules` array:

```json
{
	"FileVersion": 3,
	"EngineAssociation": "5.7",
	"Modules": [
		{
			"Name": "YourProject",
			"Type": "Runtime",
			"LoadingPhase": "Default"
		},
		{
			"Name": "PlanetGen",
			"Type": "Runtime",
			"LoadingPhase": "Default"
		}
	]
}
```

## 3. Make your game module depend on PlanetGen

Open `Source/YourProject/YourProject.Build.cs` and add `"PlanetGen"` to your dependencies, so Blueprints/code in your main game module can reference these classes:

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
	"Core", "CoreUObject", "Engine", "InputCore",
	"PlanetGen" // <-- add this
});
```

## 4. Enable the Procedural Mesh Component plugin

Edit menu -> Plugins -> search "Procedural Mesh Component" -> enable (engine-bundled, not a marketplace download). Restart the editor if prompted.

## 5. Generate project files and build

- Right-click your `.uproject` -> "Generate Visual Studio project files" (Windows) or run `xcodebuild`-equivalent generation on Mac/Linux.
- Open the generated `.sln`/`.xcworkspace`, build in **Development Editor** configuration.
- If the build succeeds, launch the editor — you should see `APlanetManager`, `APlanetChunk`, `APlanetOceanShell`, `APlanetGlowShell`, `APlanetAtmosphereController`, and `APlanetSystemManager` available as parent classes when creating new Blueprints.

If the build fails on `ProceduralMeshComponent` not found, double check step 4 — the plugin must be enabled before the module dependency in `PlanetGen.Build.cs` (`"ProceduralMeshComponent"` in `PublicDependencyModuleNames`) will resolve.
