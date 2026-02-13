#include "ComfyUIEditorModule.h"
#include "SComfyUIPanel.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FComfyUIEditorModule"

const FName FComfyUIEditorModule::ComfyUITabName = FName("ComfyUIImageGenerator");

void FComfyUIEditorModule::StartupModule()
{
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI Editor Module Started"));

    // Register tab spawner
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        ComfyUITabName,
        FOnSpawnTab::CreateRaw(this, &FComfyUIEditorModule::SpawnComfyUITab))
        .SetDisplayName(LOCTEXT("ComfyUITabTitle", "ComfyUI Generator"))
        .SetMenuType(ETabSpawnerMenuType::Hidden)
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

    // Register menu
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FComfyUIEditorModule::RegisterMenus));
}

void FComfyUIEditorModule::ShutdownModule()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ComfyUITabName);

    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
}

void FComfyUIEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    // Add to Window menu
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
    FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");

    Section.AddMenuEntry(
        FName("ComfyUIGenerator"),
        LOCTEXT("ComfyUIMenuLabel", "ComfyUI Image Generator"),
        LOCTEXT("ComfyUIMenuTooltip", "Open the ComfyUI Image Generator panel"),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]()
        {
            FGlobalTabmanager::Get()->TryInvokeTab(FComfyUIEditorModule::ComfyUITabName);
        }))
    );
}

TSharedRef<SDockTab> FComfyUIEditorModule::SpawnComfyUITab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SComfyUIPanel)
        ];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FComfyUIEditorModule, ComfyUIEditor)
