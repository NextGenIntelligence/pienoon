// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "precompiled.h"
#include "gui_menu.h"
#include "config_generated.h"
#include "character_state_machine_def_generated.h"
#include "utilities.h"

namespace fpl {
namespace pie_noon {

GuiMenu::GuiMenu() {}

static const char* TextureName(const ButtonTexture& button_texture) {
  const bool touch_screen =
      button_texture.touch_screen() != nullptr && TouchScreenDevice();
  return touch_screen ? button_texture.touch_screen()->c_str()
                      : button_texture.standard()->c_str();
}

template <class T>
static inline size_t ArrayLength(const T* array) {
  return array == nullptr ? 0 : array->Length();
}

void GuiMenu::Setup(const UiGroup* menu_def, MaterialManager* matman) {
  ClearRecentSelections();
  if (menu_def == nullptr) {
    button_list_.resize(0);
    image_list_.resize(0);
    current_focus_ = ButtonId_Undefined;
    return;  // Nothing to set up.  Just clearing things out.
  }
  assert(menu_def->cannonical_window_height() > 0);
  const size_t length_button_list = ArrayLength(menu_def->button_list());
  const size_t length_image_list = ArrayLength(menu_def->static_image_list());
  menu_def_ = menu_def;
  button_list_.resize(length_button_list);
  image_list_.resize(length_image_list);
  current_focus_ = menu_def->starting_selection();
  // Empty the queue.

  for (size_t i = 0; i < length_button_list; i++) {
    const ButtonDef* button = menu_def->button_list()->Get(i);
    button_list_[i] = TouchscreenButton();
    const size_t length_texture_normal = ArrayLength(button->texture_normal());
    for (size_t j = 0; j < length_texture_normal; j++) {
      const char* texture_name = TextureName(*button->texture_normal()->Get(j));
      button_list_[i].set_up_material(j, matman->FindMaterial(texture_name));
    }
    if (button->texture_pressed()) {
      button_list_[i].set_down_material(
          matman->FindMaterial(TextureName(*button->texture_pressed())));
    }

    const char* shader_name = (button->shader() == nullptr)
                                  ? menu_def->default_shader()->c_str()
                                  : button->shader()->c_str();
    Shader* shader = matman->FindShader(shader_name);

    const char* inactive_shader_name =
        (button->inactive_shader() == nullptr)
            ? menu_def->default_inactive_shader()->c_str()
            : button->inactive_shader()->c_str();
    Shader* inactive_shader = matman->FindShader(inactive_shader_name);

    if (shader == nullptr) {
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                  "Buttons used in menus must specify a shader!");
    }
    button_list_[i].set_shader(shader);
    button_list_[i].set_inactive_shader(inactive_shader);
    button_list_[i].set_button_def(button);
    button_list_[i].set_is_active(button->starts_active() != 0);
    button_list_[i].set_is_highlighted(true);
    button_list_[i].SetCannonicalWindowHeight(
        menu_def_->cannonical_window_height());
  }

  // Initialize image_list_.
  for (size_t i = 0; i < length_image_list; i++) {
    const StaticImageDef& image_def = *menu_def->static_image_list()->Get(i);

    const int num_textures = image_def.texture()->Length();
    std::vector<Material*> materials(num_textures);
    for (int j = 0; j < num_textures; ++j) {
      const char* material_name = TextureName(*image_def.texture()->Get(j));
      materials[j] = matman->FindMaterial(material_name);
      if (materials[j] == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Static image '%s' not found", material_name);
      }
    }
    const char* shader_name = (image_def.shader() == nullptr)
                                  ? menu_def->default_shader()->c_str()
                                  : image_def.shader()->c_str();
    Shader* shader = matman->FindShader(shader_name);
    if (shader == nullptr) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "Static image missing shader '%s'", shader_name);
    }

    image_list_[i].Initialize(image_def, materials, shader,
                              menu_def_->cannonical_window_height());
  }
}

// Force the material manager to load all the textures and shaders
// used in the UI group.
void GuiMenu::LoadAssets(const UiGroup* menu_def, MaterialManager* matman) {
  const size_t length_button_list = ArrayLength(menu_def->button_list());
  matman->LoadShader(menu_def->default_shader()->c_str());
  matman->LoadShader(menu_def->default_inactive_shader()->c_str());
  for (size_t i = 0; i < length_button_list; i++) {
    const ButtonDef* button = menu_def->button_list()->Get(i);
    const size_t length_texture_normal = ArrayLength(button->texture_normal());
    for (size_t j = 0; j < length_texture_normal; j++) {
      const char* texture_name = TextureName(*button->texture_normal()->Get(j));
      matman->LoadMaterial(texture_name);
    }
    if (button->texture_pressed()) {
      matman->LoadMaterial(TextureName(*button->texture_pressed()));
    }

    if (button->shader() != nullptr) {
      matman->LoadShader(button->shader()->c_str());
    }
    if (button->inactive_shader() != nullptr) {
      matman->LoadShader(button->inactive_shader()->c_str());
    }
  }

  const size_t length_image_list = ArrayLength(menu_def->static_image_list());
  for (size_t i = 0; i < length_image_list; i++) {
    const StaticImageDef& image_def = *menu_def->static_image_list()->Get(i);
    const size_t length_texture = ArrayLength(image_def.texture());
    for (size_t j = 0; j < length_texture; ++j) {
      matman->LoadMaterial(TextureName(*image_def.texture()->Get(j)));
    }
    if (image_def.shader() != nullptr) {
      matman->LoadShader(image_def.shader()->c_str());
    }
  }
}

void GuiMenu::AdvanceFrame(WorldTime delta_time, InputSystem* input,
                           const vec2& window_size) {
  // Start every frame with a clean list of events.
  ClearRecentSelections();
  for (size_t i = 0; i < button_list_.size(); i++) {
    TouchscreenButton& current_button = button_list_[i];
    current_button.AdvanceFrame(delta_time, input, window_size);
    current_button.set_is_highlighted(current_focus_ == current_button.GetId());

    if (current_button.IsTriggered()) {
      unhandled_selections_.push(MenuSelection(current_button.is_active()
                                                   ? current_button.GetId()
                                                   : ButtonId_InvalidInput,
                                               kTouchController));
    }
  }
}

// Utility function for finding indexes.
TouchscreenButton* GuiMenu::FindButtonById(ButtonId id) {
  for (size_t i = 0; i < button_list_.size(); i++) {
    if (button_list_[i].GetId() == id) return &button_list_[i];
  }
  return nullptr;
}

// Utility function for finding indexes.
StaticImage* GuiMenu::FindImageById(ButtonId id) {
  for (size_t i = 0; i < image_list_.size(); i++) {
    if (image_list_[i].GetId() == id) return &image_list_[i];
  }
  return nullptr;
}

// Utility function for clearing out the queue, since the syntax is weird.
void GuiMenu::ClearRecentSelections() {
  std::queue<MenuSelection>().swap(unhandled_selections_);
}

MenuSelection GuiMenu::GetRecentSelection() {
  if (unhandled_selections_.empty()) {
    return MenuSelection(ButtonId_Undefined, kUndefinedController);
  } else {
    MenuSelection return_value = unhandled_selections_.front();
    unhandled_selections_.pop();
    return return_value;
  }
}

void GuiMenu::Render(Renderer* renderer) {
  // Render touch controls, as long as the touch-controller is active.
  for (size_t i = 0; i < image_list_.size(); i++) {
    if (!image_list_[i].image_def()->render_after_buttons())
      image_list_[i].Render(*renderer);
  }
  for (size_t i = 0; i < button_list_.size(); i++) {
    button_list_[i].Render(*renderer);
  }
  for (size_t i = 0; i < image_list_.size(); i++) {
    if (image_list_[i].image_def()->render_after_buttons())
      image_list_[i].Render(*renderer);
  }
}

// Accepts logical inputs, and navigates based on it.
void GuiMenu::HandleControllerInput(uint32_t logical_input,
                                    ControllerId controller_id) {
  TouchscreenButton* current_focus_button_ = FindButtonById(current_focus_);
  if (!current_focus_button_) {
    // errors?
    return;
  }
  const ButtonDef* current_def = current_focus_button_->button_def();
  if (logical_input & LogicalInputs_Up) {
    UpdateFocus(current_def->nav_up());
  }
  if (logical_input & LogicalInputs_Down) {
    UpdateFocus(current_def->nav_down());
  }
  if (logical_input & LogicalInputs_Left) {
    UpdateFocus(current_def->nav_left());
  }
  if (logical_input & LogicalInputs_Right) {
    UpdateFocus(current_def->nav_right());
  }

  if (logical_input & LogicalInputs_Select) {
    unhandled_selections_.push(MenuSelection(current_focus_button_->is_active()
                                                 ? current_focus_
                                                 : ButtonId_InvalidInput,
                                             controller_id));
  }
  if (logical_input & LogicalInputs_Cancel) {
    unhandled_selections_.push(MenuSelection(ButtonId_Cancel, controller_id));
  }
}

// This is an internal-facing function for moving the focus around.  It
// accepts an array of possible destinations as input, and moves to
// the first visible ID it finds.  (otherwise it doesn't move.)
void GuiMenu::UpdateFocus(
    const flatbuffers::Vector<uint16_t>* destination_list) {
  // buttons are not required to provide a destination list for all directions.
  if (destination_list != nullptr) {
    for (size_t i = 0; i < destination_list->Length(); i++) {
      ButtonId destination_id = static_cast<ButtonId>(destination_list->Get(i));
      TouchscreenButton* destination = FindButtonById(destination_id);
      if (destination != nullptr && destination->is_visible()) {
        SetFocus(destination_id);
        return;
      }
    }
  }
  // if we didn't find an active button to move to, we just return and
  // leave everything unchanged.  And maybe play a noise.
  unhandled_selections_.push(
      MenuSelection(ButtonId_InvalidInput, kTouchController));
}

ButtonId GuiMenu::GetFocus() const { return current_focus_; }

void GuiMenu::SetFocus(ButtonId new_focus) { current_focus_ = new_focus; }

}  // pie_noon
}  // fpl
