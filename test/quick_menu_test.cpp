#include "QuickMenu.h"
#include <cassert>
using L=QuickMenu::Level;
using C=MenuCommand;
int main() {
    QuickMenu m;
    assert(!m.isOpen() && m.press().kind==C::none);
    // Better Thermostat: modes only.
    m.open({"off","heat"},{},"heat","");
    assert(m.level()==L::top && m.entries().size()==2 && m.selected()==1);
    assert(m.entries()[1].label=="Tryb" && m.entries()[1].detail=="Grzanie" && m.entries()[1].icon==MenuIcon::heat);
    m.rotate(5); assert(m.selected()==1);  // Clamped, no wrap-around.
    m.rotate(-5); assert(m.selected()==0);
    assert(m.press().kind==C::close && !m.isOpen());

    // Versatile Thermostat: modes, presets and eco shortcut.
    std::vector<std::string> modes{"off","heat","cool"}, presets{"none","eco","comfort","boost"};
    m.open(modes,presets,"heat","comfort");
    assert(m.entries().size()==4);
    assert(m.entries()[2].label=="Profil" && m.entries()[2].detail=="Komfort");
    assert(m.entries()[3].label=="Eco" && m.entries()[3].detail=="Wyłączone");
    // Mode list starts on the active mode; turning alone sends nothing.
    assert(m.press().kind==C::none && m.level()==L::list && m.heading()=="TRYB");
    assert(m.entries().size()==4 && m.selected()==2 && m.entries()[2].active);
    m.rotate(1);
    auto command=m.press();
    assert(command.kind==C::mode && command.value=="cool" && m.isOpen());
    // "Back" returns to the item the list was opened from.
    m.rotate(-10); assert(m.selected()==0);
    assert(m.press().kind==C::none && m.level()==L::top && m.selected()==1);
    // Profile list.
    m.rotate(1); m.press();
    assert(m.heading()=="PROFIL" && m.entries()[m.selected()].value=="comfort");
    m.rotate(-1); command=m.press();
    assert(command.kind==C::preset && command.value=="eco");
    // Eco toggles: on → eco, already eco → none.
    m.open(modes,presets,"heat","comfort"); m.rotate(2);
    command=m.press(); assert(command.kind==C::preset && command.value=="eco");
    m.open(modes,presets,"heat","eco"); m.rotate(2);
    assert(m.entries()[3].detail=="Włączone");
    command=m.press(); assert(command.kind==C::preset && command.value=="none");
    // Without "none" the eco item opens the profile list instead.
    m.open(modes,{"eco","comfort"},"heat","eco"); m.rotate(2);
    assert(m.press().kind==C::none && m.level()==L::list && m.heading()=="PROFIL");
    // Presets only, unknown names stay raw.
    m.open({},{"custom"},"",""); assert(m.entries().size()==2 && m.selected()==1);
    m.press(); assert(m.entries()[1].label=="custom" && m.selected()==1);
}
