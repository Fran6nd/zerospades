/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "UIFramework.as"

namespace spades {
	namespace ui {

		class ButtonBase : UIElement {
			bool Pressed = false;
			bool Hover = false;
			bool Toggled = false;
			bool Toggle = false;
			bool Repeat = false;
			bool ActivateOnMouseDown = false;

			EventHandler@ Activated;
			EventHandler@ DoubleClicked;
			EventHandler@ RightClicked;
			string Caption;
			string ActivateHotKey;

			private Timer@ repeatTimer;

			// for double click detection
			private float lastActivate = -1.0F;
			private Vector2 lastActivatePosition = Vector2();

			ButtonBase(UIManager@ manager) {
				super(manager);
				IsMouseInteractive = true;
				@repeatTimer = Timer(Manager);
				@repeatTimer.Tick = TimerTickEventHandler(this.RepeatTimerFired);
			}

			void PlayMouseEnterSound() { Manager.PlaySound("Sounds/Feedback/Limbo/Hover.opus"); }
			void PlayActivateSound() { Manager.PlaySound("Sounds/Feedback/Limbo/Select.opus"); }

			void OnActivated() {
				if (Activated !is null)
					Activated(this);
			}

			void OnDoubleClicked() {
				if (DoubleClicked !is null)
					DoubleClicked(this);
			}

			void OnRightClicked() {
				if (RightClicked !is null)
					RightClicked(this);
			}

			private void RepeatTimerFired(Timer@ timer) {
				OnActivated();
				timer.Interval = 0.1F;
			}

			void MouseDown(MouseButton button, Vector2 clientPosition) {
				if (button != spades::ui::MouseButton::LeftMouseButton and
					button != spades::ui::MouseButton::RightMouseButton) {
					return;
				}

				PlayActivateSound();
				if (button == spades::ui::MouseButton::RightMouseButton) {
					OnRightClicked();
					return;
				}

				Pressed = true;
				Hover = true;

				if (Repeat or ActivateOnMouseDown) {
					OnActivated();
					if (Repeat) {
						repeatTimer.Interval = 0.3F;
						repeatTimer.Start();
					}
				}
			}
			void MouseMove(Vector2 clientPosition) {
				if (Pressed) {
					bool newHover = AABB2(Vector2(0.0F, 0.0F), Size).Contains(clientPosition);
					if (newHover != Hover) {
						if (Repeat) {
							if (newHover) {
								OnActivated();
								repeatTimer.Interval = 0.3F;
								repeatTimer.Start();
							} else {
								repeatTimer.Stop();
							}
						}

						Hover = newHover;
					}
				}
			}
			void MouseUp(MouseButton button, Vector2 clientPosition) {
				if (button != spades::ui::MouseButton::LeftMouseButton and
					button != spades::ui::MouseButton::RightMouseButton) {
					return;
				}

				if (Pressed) {
					Pressed = false;
					if (Hover and not(Repeat or ActivateOnMouseDown)) {
						if (Toggle)
							Toggled = !Toggled;

						OnActivated();
						if (Manager.Time - lastActivate < 0.35F and
							(clientPosition - lastActivatePosition).ManhattanLength < 10.0F) {
							OnDoubleClicked();
						}

						lastActivate = Manager.Time;
						lastActivatePosition = clientPosition;
					}

					if (Repeat and Hover)
						repeatTimer.Stop();
				}
			}
			void MouseEnter() {
				Hover = true;
				if (not Pressed)
					PlayMouseEnterSound();
				UIElement::MouseEnter();
			}
			void MouseLeave() {
				Hover = false;
				UIElement::MouseLeave();
			}

			void KeyDown(string key) {
				if (key == " ")
					OnActivated();
				UIElement::KeyDown(key);
			}
			void KeyUp(string key) { UIElement::KeyUp(key); }

			void HotKey(string key) {
				if (key == ActivateHotKey)
					OnActivated();
			}
		}

		class SimpleButton : spades::ui::Button {
			Vector4 TextColor = Vector4(1, 1, 1, 1);
			Vector4 DisabledTextColor = Vector4(1.0F, 1.0F, 1.0F, 0.4F);

			SimpleButton(spades::ui::UIManager@ manager) { super(manager); }

			void Render() {
				// Appearance lives in C++ (shared with the editor); see
				// Gui/UIWidgetPainter and ScriptBindings/UIWidgetPainterScript.
				PaintSimpleButton(Manager.Renderer, this.Font, ScreenPosition, Size, Caption,
					Alignment, TextColor, DisabledTextColor, IsEnabled, Hover, Pressed, Toggled);
			}
		}

		class CheckBox : spades::ui::Button {
			CheckBox(spades::ui::UIManager @manager) {
				super(manager);
				this.Toggle = true;
			}
			void Render() {
				PaintCheckBox(Manager.Renderer, this.Font, ScreenPosition, Size, Caption,
					Hover, Pressed, Toggled);
			}
		}

		class RadioButton : spades::ui::Button {
			string GroupName;

			RadioButton(spades::ui::UIManager @manager) {
				super(manager);
				this.Toggle = true;
			}
			void Check() {
				this.Toggled = true;

				// uncheck others
				if (GroupName.length > 0) {
					UIElement @[] @children = this.Parent.GetChildren();
					for (uint i = 0, count = children.length; i < children.length; i++) {
						RadioButton@ btn = cast<RadioButton>(children[i]);
						if (btn is this)
							continue;
						if (btn !is null) {
							if (GroupName == btn.GroupName)
								btn.Toggled = false;
						}
					}
				}
			}
			void OnActivated() {
				Check();

				Button::OnActivated();
			}
			void Render() {
				PaintRadioButton(Manager.Renderer, this.Font, ScreenPosition, Size, Caption,
					this.Enable, Hover, Pressed, Toggled);
			}
		}

		class Button : ButtonBase {
			Vector2 Alignment = Vector2(0.5F, 0.5F);

			string HotKeyText;
			Vector2 HotKeyTextAlignment = Vector2(1.0F, 0.5F);

			Button(UIManager@ manager) {
				super(manager);
			}

			void Render() {
				// Preserve the original side effect: a button with a hotkey label
				// left-aligns its caption from then on.
				if (HotKeyText.length > 0)
					Alignment = Vector2(0.0F, 0.5F);
				PaintButton(Manager.Renderer, this.Font, ScreenPosition, Size, Caption, Alignment,
					HotKeyText, HotKeyTextAlignment, IsEnabled, Hover, Pressed, Toggled);
			}
		}
	}
}
