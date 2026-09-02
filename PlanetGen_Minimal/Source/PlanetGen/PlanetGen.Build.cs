using UnrealBuildTool;

public class PlanetGen : ModuleRules
{
	public PlanetGen(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ProceduralMeshComponent" // chunk meshes, ocean shell, glow shell
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI"
		});

		// UE::Tasks (TaskGraph) and AsyncTask live in Core — no extra module needed.
		// USkyAtmosphereComponent / UVolumetricCloudComponent / UExponentialHeightFogComponent
		// live in Engine — already covered above.

		bUseUnity = false; // optional, easier debugging on a module this size
	}
}
