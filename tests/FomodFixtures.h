// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2026 BetterNMM Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QByteArray>

const QByteArray g_config = R"(<?xml version="1.0" encoding="UTF-16"?>
<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <moduleName>Example Overhaul</moduleName>
  <requiredInstallFiles>
    <folder source="00 Core" destination="" />
  </requiredInstallFiles>
  <installSteps order="Explicit">
    <installStep name="Texture size">
      <optionalFileGroups order="Explicit">
        <group name="Resolution" type="SelectExactlyOne">
          <plugins order="Explicit">
            <plugin name="2K">
              <description>Two thousand pixels</description>
              <files><folder source="10 2K\textures" destination="textures" priority="0" /></files>
              <conditionFlags><flag name="res">2k</flag></conditionFlags>
              <typeDescriptor><type name="Recommended"/></typeDescriptor>
            </plugin>
            <plugin name="4K">
              <description>Four thousand pixels</description>
              <files><folder source="11 4K/textures" destination="textures" /></files>
              <conditionFlags><flag name="res">4k</flag></conditionFlags>
              <typeDescriptor><type name="Optional"/></typeDescriptor>
            </plugin>
          </plugins>
        </group>
        <group name="Extras" type="SelectAny">
          <plugins order="Ascending">
            <plugin name="Zz patch">
              <files><file source="20 Patch\Patch.esp" /></files>
              <typeDescriptor>
                <dependencyType>
                  <defaultType name="NotUsable"/>
                  <patterns>
                    <pattern>
                      <dependencies operator="And"><fileDependency file="Dawnguard.esm" state="Active"/></dependencies>
                      <type name="Recommended"/>
                    </pattern>
                  </patterns>
                </dependencyType>
              </typeDescriptor>
            </plugin>
            <plugin name="Aa readme">
              <files><file source="readme.txt" destination="docs/readme.txt" alwaysInstall="false"/></files>
              <typeDescriptor><type name="Optional"/></typeDescriptor>
            </plugin>
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
    <installStep name="Only for 4K">
      <visible><flagDependency flag="res" value="4k"/></visible>
      <optionalFileGroups>
        <group name="Parallax" type="SelectAll">
          <plugins><plugin name="Parallax"><files><file source="30 Parallax/p.dds" destination="textures/p.dds"/></files>
          <typeDescriptor><type name="Required"/></typeDescriptor></plugin></plugins>
        </group>
      </optionalFileGroups>
    </installStep>
  </installSteps>
  <conditionalFileInstalls>
    <patterns>
      <pattern>
        <dependencies operator="Or"><flagDependency flag="res" value="4k"/><fileDependency file="Nope.esp" state="Active"/></dependencies>
        <files><file source="40 Conditional/hd.ini" destination="SKSE/Plugins/hd.ini" priority="5"/></files>
      </pattern>
    </patterns>
  </conditionalFileInstalls>
</config>
)";
