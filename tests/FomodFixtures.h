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


// Like the lighting mods that needed patches: the main option sets a flag a patch depends on, a flag-only option turns on
// files further down, a step shows only when another mod is there, and an automatic install follows a plugin
const QByteArray g_lights = R"(<?xml version="1.0"?>
<config>
  <moduleName>Lights</moduleName>
  <requiredInstallFiles><file source="Core/Lights.esp" destination="Lights.esp"/></requiredInstallFiles>
  <installSteps order="Explicit">
    <installStep name="Main">
      <optionalFileGroups order="Explicit">
        <group name="Version" type="SelectExactlyOne">
          <plugins order="Explicit">
            <plugin name="Standard"><files><file source="Main/Standard.esp" destination="Lights - Main.esp"/></files>
              <conditionFlags><flag name="HD">active</flag></conditionFlags><typeDescriptor><type name="Recommended"/></typeDescriptor></plugin>
            <plugin name="Lite"><files><file source="Main/Lite.esp" destination="Lights - Main.esp"/></files>
              <typeDescriptor><type name="Optional"/></typeDescriptor></plugin>
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
    <installStep name="Patches">
      <optionalFileGroups order="Explicit">
        <group name="Weather" type="SelectExactlyOne">
          <plugins order="Explicit">
            <plugin name="Vanilla weather"><files><file source="Patches/Vanilla.esp" destination="Lights - Vanilla Weather.esp"/></files>
              <typeDescriptor><type name="Optional"/></typeDescriptor></plugin>
            <plugin name="Weather patch"><files><file source="Patches/Weather.esp" destination="Lights - Weather.esp"/></files>
              <typeDescriptor><dependencyType><defaultType name="Optional"/><patterns><pattern>
                <dependencies operator="And"><flagDependency flag="HD" value="active"/><fileDependency file="Weather.esp" state="Active"/></dependencies>
                <type name="Recommended"/></pattern></patterns></dependencyType></typeDescriptor></plugin>
          </plugins>
        </group>
        <group name="Extras" type="SelectAny">
          <plugins order="Explicit">
            <plugin name="Citizens patch"><conditionFlags><flag name="Citizens">On</flag></conditionFlags>
              <typeDescriptor><dependencyType><defaultType name="NotUsable"/><patterns><pattern>
                <dependencies><fileDependency file="Citizens.esp" state="Active"/></dependencies>
                <type name="Recommended"/></pattern></patterns></dependencyType></typeDescriptor></plugin>
            <plugin name="Readme"><files><file source="readme.txt" destination="docs/readme.txt"/></files>
              <typeDescriptor><type name="Recommended"/></typeDescriptor></plugin>
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
    <installStep name="Water">
      <visible><dependencies><fileDependency file="Water.esp" state="Active"/></dependencies></visible>
      <optionalFileGroups>
        <group name="Water" type="SelectAny">
          <plugins><plugin name="Water patch"><files><file source="Patches/Water.esp" destination="Lights - Water.esp"/></files>
            <typeDescriptor><type name="Recommended"/></typeDescriptor></plugin></plugins>
        </group>
      </optionalFileGroups>
    </installStep>
  </installSteps>
  <conditionalFileInstalls>
    <patterns>
      <pattern><dependencies><flagDependency flag="Citizens" value="On"/></dependencies>
        <files><folder source="Patches/Citizens" destination=""/></files></pattern>
      <pattern><dependencies><fileDependency file="Snow.esp" state="Active"/></dependencies>
        <files><file source="Patches/Snow.esp" destination="Lights - Snow.esp"/></files></pattern>
    </patterns>
  </conditionalFileInstalls>
</config>
)";

const QByteArray g_escape = R"(<?xml version="1.0"?>
<config>
  <moduleName>Escape</moduleName>
  <installSteps order="Explicit"><installStep name="Main"><optionalFileGroups><group name="Patches" type="SelectAny"><plugins>
    <plugin name="Evil patch"><files><file source="evil.esp" destination="../../evil.esp"/></files>
      <typeDescriptor><dependencyType><defaultType name="Optional"/><patterns><pattern>
        <dependencies><fileDependency file="Weather.esp" state="Active"/></dependencies><type name="Recommended"/></pattern></patterns>
      </dependencyType></typeDescriptor></plugin>
  </plugins></group></optionalFileGroups></installStep></installSteps>
</config>
)";

const QByteArray g_release = R"({"tag_name":"v1.2.3","html_url":"https://github.com/o/r/releases/tag/v1.2.3","body":"notes",
  "assets":[{"name":"BetterNMM-1.2.3.zip","browser_download_url":"https://x/zip","size":5},
            {"name":"BetterNMM-Setup-1.2.3.exe","browser_download_url":"https://x/setup.exe","size":166676508}]})";
const QByteArray g_nightly = R"({"tag_name":"nightly"})";
