//  $Id$
//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2006 SuperTuxKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include <sstream>
#include <string>

#include "loader.hpp"
#include "char_sel.hpp"
#include "kart_properties_manager.hpp"
#include "preprocessor.hpp"
#include "widget_set.hpp"
#include "race_manager.hpp"
#include "start_screen.hpp"
#include "user_config.hpp"
#include "menu_manager.hpp"
#include "kart_properties.hpp"
#include "material_manager.hpp"
#include "material.hpp"
#include "translation.hpp"

CharSel::CharSel(int whichPlayer)
        : m_kart(0), m_player_index(whichPlayer)
{
    // for some strange reasons plib calls makeCurrent() in ssgContextf
    // constructor, so we have to save the old one here and restore it
    ssgContext* oldContext = ssgGetCurrentContext();
    m_context = new ssgContext;
    oldContext->makeCurrent();

    m_menu_id = widgetSet -> vstack(0);
    const int HS1 = widgetSet -> hstack (m_menu_id);
    widgetSet -> filler(HS1);

    // Due to widgetSet constraints, this must be static!
    static char HEADING[MAX_MESSAGE_LENGTH];
    snprintf(HEADING, sizeof(HEADING), _("Player %d, choose a driver"),
             m_player_index + 1);

    widgetSet -> label(HS1, HEADING, GUI_LRG, GUI_ALL, 0, 0);
    widgetSet -> filler(HS1);

    widgetSet -> filler(m_menu_id);

    const int HA = widgetSet -> harray(m_menu_id);
    const int ICON_SIZE = 64;

    for (unsigned int i = 0; i < kart_properties_manager->getNumberOfKarts(); i++)
    {
        const KartProperties* kp= kart_properties_manager->getKartById(i);
        Material *m = material_manager->getMaterial(kp->getIconFile());
        const int C = widgetSet->image(HA, m->getState()->getTextureHandle(),
                                 ICON_SIZE, ICON_SIZE);
        widgetSet->activate_widget(C, i, 0);

        if (i == kart_properties_manager->getNumberOfKarts() - 1)
            widgetSet->set_active(C);
    }
    const int HS2 = widgetSet -> hstack(m_menu_id);
    widgetSet -> filler (HS2);
    m_kart_name_label = widgetSet -> label(HS2, _("No driver choosed"),
                                           GUI_MED, GUI_ALL);
    widgetSet -> filler (HS2);
    widgetSet -> layout(m_menu_id, 0, 1);

    m_current_kart = -1;
    switchCharacter(1);

    m_clock = 0;
    //test

}

//-----------------------------------------------------------------------------
CharSel::~CharSel()
{
    widgetSet -> delete_widget(m_menu_id) ;
    ssgDeRefDelete(m_kart);

    delete m_context;
}

//-----------------------------------------------------------------------------
void CharSel::switchCharacter(int n)
{
    const KartProperties* kp= kart_properties_manager->getKartById(n);
    if (m_current_kart != n && kp != NULL)
    {
        widgetSet -> set_label(m_kart_name_label, kp->getName().c_str());

        m_current_kart = n;
        ssgDeRefDelete(m_kart);
        m_kart = new ssgTransform;
        m_kart->ref();
        ssgEntity* kartentity = kp->getModel();

        m_kart->addKid(kartentity);

        preProcessObj ( m_kart, 0 );
    }
}

//-----------------------------------------------------------------------------
void CharSel::update(float dt)
{
    m_clock += dt * 40.0f;
    BaseGUI::update(dt);

    switchCharacter(widgetSet->token(widgetSet->click()));

    if (m_kart != NULL)
    {
        ssgContext* oldContext = ssgGetCurrentContext();
        m_context -> makeCurrent();

        glClear(GL_DEPTH_BUFFER_BIT);
        // FIXME: A bit hackish...
        glViewport ( 0, 0, 800, 320);

        m_context -> setFOV ( 45.0f, 45.0f * 320.0f/800.0f ) ;
        m_context -> setNearFar ( 0.05f, 1000.0f ) ;

        sgCoord cam_pos;
        sgSetCoord(&cam_pos, 0, 0, 0, 0, 0, 0);
        m_context -> setCamera ( &cam_pos ) ;

        glEnable (GL_DEPTH_TEST);
        sgCoord trans;
        sgSetCoord(&trans, 0, 3, -.4f, m_clock, 0, 0);
        m_kart->setTransform (&trans) ;
        //glShadeModel(GL_SMOOTH);
        ssgCullAndDraw ( m_kart ) ;
        glViewport ( 0, 0, user_config->m_width, user_config->m_height ) ;

        glDisable (GL_DEPTH_TEST);
        oldContext->makeCurrent();
    }
}

//----------------------------------------------------------------------------
void CharSel::select()
{
    const int TOKEN = widgetSet -> token (widgetSet -> click());
    const KartProperties* KP = kart_properties_manager->getKartById(TOKEN);
    if (KP != NULL)
    {
        race_manager->setPlayerKart(m_player_index, KP->getIdent());
        user_config->m_player[m_player_index].setLastKartId(TOKEN);
    }

    if (race_manager->getNumPlayers() > 1)
    {
        if (menu_manager->isCurrentMenu(MENUID_CHARSEL_P1))
        {
            menu_manager->pushMenu(MENUID_CHARSEL_P2);
            return;
        }
    }

    if (race_manager->getNumPlayers() > 2)
    {
        if (menu_manager->isCurrentMenu(MENUID_CHARSEL_P2))
        {
            menu_manager->pushMenu(MENUID_CHARSEL_P3);
            return;
        }
    }

    if (race_manager->getNumPlayers() > 3)
    {
        if (menu_manager->isCurrentMenu(MENUID_CHARSEL_P3))
        {
            menu_manager->pushMenu(MENUID_CHARSEL_P4);
            return;
        }
    }

    if (race_manager->getRaceMode() != RaceSetup::RM_GRAND_PRIX)
        menu_manager->pushMenu(MENUID_TRACKSEL);
    else
        startScreen->switchToGame();
}
