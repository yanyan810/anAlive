#pragma once
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
#include "WeaponEditor.h"
#include "imgui.h"
#include <functional>

enum class WeaponEditorMode { Edit, Test };

class WeaponEditorUI {
public:
    bool visible=false;
    WeaponEditorMode mode=WeaponEditorMode::Edit;
    bool returnToGame=false;
    const WeaponDefinition* Selected() const {
        return selected_>=0 && static_cast<size_t>(selected_)<model_.entries.size()
            ? &model_.entries[static_cast<size_t>(selected_)].value : nullptr;
    }
    std::optional<WeaponDefinition> TakeTestRequest() {
        auto request=testRequest_; testRequest_.reset(); return request;
    }
    // A value signals a successful save; nonempty value requests the shared Debug Equip path.
    std::optional<std::string> Draw(WeaponSystem& live,const std::string& level,bool canEquip, const std::function<void()>& preview={}, const std::function<void()>& showroomControls={}) {
        if (!visible || mode==WeaponEditorMode::Test) return std::nullopt;
        std::optional<std::string> result;
        ImGuiWindowFlags flags=0;
        if (preview) {
            const auto* viewport=ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            flags=ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoDocking;
        } else ImGui::SetNextWindowSize({1080,660},ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Weapon Editor",preview ? nullptr : &visible,flags)) { ImGui::End(); return result; }
        if (!model_.IsOpen()) {
            if (!model_.Open(path_,level)) {
                ImGui::TextWrapped("%s",model_.error.c_str()); ImGui::End(); return result;
            }
        }
        ImGui::TextUnformatted(model_.Dirty()?"Unsaved Changes":"Saved");
        ImGui::SameLine(); ImGui::TextUnformatted("resources/Data/weapons.json");
        if (ImGui::Button("Save")) {
            if (model_.Save(level,live)) result=std::string{};
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!canEquip || selected_<0 || static_cast<size_t>(selected_)>=model_.entries.size());
        if (ImGui::Button("Save & Equip")) {
            const auto id=model_.entries[static_cast<size_t>(selected_)].value.id;
            if (model_.Save(level,live)) result=id;
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Revert")) ImGui::OpenPopup("Revert all edits?");
        if (ImGui::BeginPopupModal("Revert all edits?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Discard all drafts and reload weapons.json from disk?");
            if (ImGui::Button("Revert from disk")) {
                if (model_.Open(path_,level)) { selected_=0; ImGui::CloseCurrentPopup(); }
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (preview) {
            ImGui::SameLine();
            ImGui::BeginDisabled(!Selected() || !model_.Validate().empty());
            if (ImGui::Button("Test Weapon")) testRequest_=*Selected();
            ImGui::EndDisabled(); ImGui::SameLine();
            if (ImGui::Button("Return to Game")) returnToGame=true;
            if (showroomControls) showroomControls();
        }
        if (!model_.error.empty()) ImGui::TextWrapped("Error: %s",model_.error.c_str());
        const auto validation=model_.Validate();
        if (!validation.empty()) ImGui::TextWrapped("Validation: %s",validation.c_str());
        ImGui::Separator();
        // Resizable list / tabbed properties / summary; panes scroll independently.
        if (ImGui::BeginTable("Weapon editor panes",3,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Weapon List",ImGuiTableColumnFlags_WidthStretch,1);
            ImGui::TableSetupColumn(preview ? "Weapon Preview" : "Settings",ImGuiTableColumnFlags_WidthStretch,2.2f);
            ImGui::TableSetupColumn(preview ? "Parameters" : "Summary",ImGuiTableColumnFlags_WidthStretch,2);
            ImGui::TableHeadersRow();
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            ImGui::BeginChild("List",{0,0});
            search_.Draw("Search",-1);
            if (ImGui::Button("New Weapon")) { duplicate_=-1; newId_.clear(); newName_.clear(); ImGui::OpenPopup("Create Weapon"); }
            const bool hasSelection=selected_>=0 && static_cast<size_t>(selected_)<model_.entries.size();
            ImGui::BeginDisabled(!hasSelection);
            if (ImGui::Button("Duplicate")) {
                duplicate_=selected_; newId_.clear(); newName_=model_.entries[static_cast<size_t>(selected_)].value.displayName;
                ImGui::OpenPopup("Create Weapon");
            }
            ImGui::SameLine(); if (ImGui::Button("Delete")) ImGui::OpenPopup("Delete Weapon?");
            ImGui::EndDisabled();
            if (ImGui::BeginPopupModal("Create Weapon",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                if (duplicate_>=0) ImGui::Text("Source: %s",model_.entries[static_cast<size_t>(duplicate_)].value.id.c_str());
                Text("New ID",newId_); Text("Display Name",newName_);
                ImGui::TextUnformatted("ID: letter first; letters, digits, '_' or '-'; max 64.");
                if (ImGui::Button("Create")) {
                    if (model_.Create(newId_,newName_,duplicate_)) {
                        selected_=static_cast<int>(model_.entries.size())-1; ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                if (!model_.error.empty()) ImGui::TextWrapped("%s",model_.error.c_str());
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupModal("Delete Weapon?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Delete %s from the draft?",model_.entries[static_cast<size_t>(selected_)].value.id.c_str());
                ImGui::TextUnformatted("Other stages / Blender files may reference this ID.");
                ImGui::TextUnformatted("Save checks the current stage and active pickups only.");
                ImGui::TextUnformatted("An equipped deleted weapon switches to Initial Weapon after Save.");
                if (ImGui::Button("Delete from draft")) {
                    model_.entries.erase(model_.entries.begin()+selected_); selected_=0; ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            for (size_t i=0;i<model_.entries.size();++i) {
                const auto& w=model_.entries[i].value;
                const auto label=w.id+" / "+w.displayName;
                if (!search_.PassFilter(label.c_str())) continue;
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(label.c_str(),selected_==static_cast<int>(i))) selected_=static_cast<int>(i);
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (preview) {
                ImGui::TableSetColumnIndex(1); ImGui::BeginChild("3D Preview",{0,0});
                ImGui::TextUnformatted("Showroom / Weapon Preview");
                ImGui::TextWrapped("Test Weapon: LMB fire | RMB ADS | R reload | ESC editor");
                preview(); ImGui::EndChild();
            }
            ImGui::TableSetColumnIndex(preview ? 2 : 1); ImGui::BeginChild("Properties",{0,0});
            if (const auto* selected=Selected()) {
                ImGui::TextWrapped("Editing: %s / %s",selected->id.c_str(),selected->displayName.c_str());
                ImGui::Separator();
                DrawFields(model_.entries[static_cast<size_t>(selected_)].value,selected_);
            } else {
                ImGui::TextUnformatted("Select a weapon from the Weapon List to edit.");
            }
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Game Start Settings")) {
                ImGui::TextWrapped("Initial Weapon sets the starting loadout. Select the weapon to edit in the Weapon List.");
                if (ImGui::BeginCombo("Initial Weapon",model_.initialWeapon.c_str())) {
                    for (const auto& entry:model_.entries)
                        if (ImGui::Selectable(entry.value.id.c_str(),model_.initialWeapon==entry.value.id)) model_.initialWeapon=entry.value.id;
                    ImGui::EndCombo();
                }
            }
            if (!preview) { ImGui::EndChild(); ImGui::TableSetColumnIndex(2); ImGui::BeginChild("Summary",{0,0}); }
            else ImGui::Separator();
            if (selected_>=0 && static_cast<size_t>(selected_)<model_.entries.size()) Summary(model_.entries[static_cast<size_t>(selected_)].value);
            ImGui::EndChild(); ImGui::EndTable();
        }
        ImGui::End(); return result;
    }
private:
    WeaponEditor model_;
    std::optional<WeaponDefinition> testRequest_;
    ImGuiTextFilter search_;
    int selected_=0,duplicate_=-1;
    std::string newId_,newName_;
    const std::string path_="resources/Data/weapons.json";
    static void Text(const char* label,std::string& value) {
        std::vector<char> buffer(value.size()+256,0);
        std::copy(value.begin(),value.end(),buffer.begin());
        if (ImGui::InputText(label,buffer.data(),buffer.size())) value=buffer.data();
    }
    static void DrawFields(WeaponDefinition& w,int selection) {
        if (!ImGui::BeginTabBar("Categories")) return;
        if (ImGui::BeginTabItem("General")) {
            ImGui::PushID(selection);
            Text("ID",w.id); Text("Display Name",w.displayName);
            ImGui::TextWrapped("Changing ID does not rename stage or Blender references.");
            int slot=static_cast<int>(w.slot); if (ImGui::Combo("Slot",&slot,"Main\0Sub\0")) w.slot=static_cast<WeaponSlot>(slot);
            Text("Type",w.type);
            ImGui::SliderInt("Rarity (stars)",&w.rarity,1,5);
            ImGui::InputDouble("Weight",&w.spawnWeight);
            float scale[]{w.pickupScale.x,w.pickupScale.y,w.pickupScale.z};
            if (ImGui::InputFloat3("Pickup Scale",scale)) w.pickupScale={scale[0],scale[1],scale[2]};
            float color[]{w.pickupColor.x,w.pickupColor.y,w.pickupColor.z};
            if (ImGui::ColorEdit3("Pickup Color",color)) w.pickupColor={color[0],color[1],color[2]};
            ImGui::PopID(); ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Fire")) {
            ImGui::PushID(selection);
            int mode=static_cast<int>(w.fireMode);
            if (ImGui::Combo("Fire Mode",&mode,"SemiAuto\0FullAuto\0Burst\0")) w.fireMode=static_cast<WeaponFireMode>(mode);
            ImGui::InputFloat("Damage / Pellet",&w.damage); ImGui::InputFloat("Fire Interval (s)",&w.fireInterval);
            ImGui::InputFloat("Range",&w.range); ImGui::InputInt("Ammo Per Shot",&w.ammoPerShot);
            ImGui::InputInt("Pellet Count",&w.pelletCount);
            if (w.fireMode==WeaponFireMode::Burst) {
                ImGui::InputInt("Burst Count",&w.burstCount); ImGui::InputFloat("Burst Interval (s)",&w.burstInterval);
            }
            ImGui::InputFloat("Bullet Speed",&w.bulletSpeed); ImGui::InputFloat("Bullet Lifetime (s)",&w.bulletLifeTime);
            ImGui::PopID(); ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Ammo")) {
            ImGui::PushID(selection);
            ImGui::InputInt("Magazine Size",&w.magazineSize); ImGui::InputInt("Reserve Ammo",&w.reserveAmmo);
            ImGui::InputInt("Max Reserve Ammo",&w.maxReserveAmmo); ImGui::InputInt("Ammo Per Shot",&w.ammoPerShot);
            ImGui::PopID(); ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Reload")) {
            ImGui::PushID(selection);
            int mode=static_cast<int>(w.reloadMode);
            if (ImGui::Combo("Reload Mode",&mode,"Magazine\0PerRound\0")) w.reloadMode=static_cast<WeaponReloadMode>(mode);
            if (w.reloadMode==WeaponReloadMode::Magazine) ImGui::InputFloat("Reload Time (s)",&w.reloadTime);
            else {
                ImGui::InputFloat("Reload Start Time",&w.reloadStartTime);
                ImGui::InputFloat("Reload Per Round Time",&w.reloadPerRoundTime);
                ImGui::InputFloat("Reload End Time",&w.reloadEndTime);
                ImGui::Checkbox("Reload Can Interrupt",&w.reloadCanInterrupt);
            }
            ImGui::PopID(); ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ADS / Accuracy")) {
            ImGui::PushID(selection);
            ImGui::InputFloat("Hip Spread Degrees",&w.hipSpreadDegrees); ImGui::InputFloat("ADS Spread Degrees",&w.adsSpreadDegrees);
            ImGui::InputFloat("ADS FOV",&w.adsFovDegrees); ImGui::InputFloat("ADS Transition Time",&w.adsTransitionTime);
            ImGui::InputFloat("ADS Sensitivity Multiplier",&w.adsSensitivityMultiplier);
            ImGui::PopID(); ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    static void Summary(const WeaponDefinition& w) {
        ImGui::TextWrapped("%s (%s)",w.displayName.c_str(),w.id.c_str());
        ImGui::Text("%s | Rarity %d/5",WeaponSlotName(w.slot),w.rarity);
        ImGui::TextWrapped("Type: %s",w.type.c_str());
        ImGui::Separator();
        ImGui::Text("RPM (60 / fireInterval): %.1f",w.fireInterval>0?60.0/w.fireInterval:0);
        if (w.fireMode==WeaponFireMode::Burst) {
            ImGui::Text("Burst: %d shots / %.3f s interval",w.burstCount,w.burstInterval);
            ImGui::TextWrapped("RPM above uses the post-burst cooldown, not the interval within a burst.");
        }
        ImGui::Text("Pellet Count: %d",w.pelletCount); ImGui::Text("Damage / Pellet: %.2f",w.damage);
        ImGui::Text("All Pellets Damage: %.2f",static_cast<double>(w.damage)*w.pelletCount);
        ImGui::Text("Shots / Magazine: %d",w.ammoPerShot>0?w.magazineSize/w.ammoPerShot:0);
        ImGui::Text("Initial Total Ammo: %lld",static_cast<long long>(w.magazineSize)+std::min(w.reserveAmmo,w.maxReserveAmmo));
        ImGui::Separator();
        ImGui::TextWrapped("Test Weapon in Showroom uses unsaved parameters. ESC returns to the editor.");
        ImGui::TextWrapped("Save preserves current pickups. Spawn Filter changes affect future stage starts.");
    }
};
#endif
