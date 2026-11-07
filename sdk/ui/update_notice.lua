-- SPDX-License-Identifier: GPL-3.0-or-later
-- Owned main-menu extension. Delivered by Workshop, including to older DLLs.
return function(env, latest)
    local function version(text)
        if type(text)~='string' then return end
        local a,b,c=text:match('^(%d+)%.(%d+)%.(%d+)')
        if a then return {tonumber(a),tonumber(b),tonumber(c)} end
    end
    local installed=type(env.cc2x)=='table' and env.cc2x.version or nil
    local current,target=version(installed),version(latest)
    if not target then return false end
    if current then
        for i=1,3 do
            if current[i]>target[i] then return false end
            if current[i]<target[i] then break end
            if i==3 then return false end
        end
    end
    if env.__cc2x_update_notice then return false end
    env.__cc2x_update_notice=true
    local modal,instructions,ui=true,false,nil
    local wrapped=nil
    local function main()
        return type(env.g_screens)=='table' and env.g_screen_index==env.g_screens.main
            and env.g_is_menu_active~=false
    end
    local function active()return modal and main()end
    local function ensure_ui()
        if not ui and type(env.lib_imgui)=='table' and type(env.lib_imgui.create_ui)=='function' then
            ui=env.lib_imgui:create_ui()
        end
        return ui~=nil
    end
    local function banner()
        local base=env.g_ui
        if type(base)~='table' or base==wrapped or type(base.header)~='function' then return end
        wrapped=base
        local header,shown=base.header,false
        base.header=function(self,...)
            if main() and not shown then
                shown=true
                header(self,'CC2X SDK UPDATE AVAILABLE')
                self:text_basic(tostring(installed or 'UNKNOWN')..' -> '..latest,env.color_status_bad or env.color_white)
                self:text_basic('Close game; update with CC2 ModLoader.',env.color_white)
                if self:button('SDK UPDATE INSTRUCTIONS',true) then modal=true;instructions=true end
                self:spacer(4)
            end
            return header(self,...)
        end
        return function()shown=false end
    end
    local reset_banner
    local original_update=env.update
    if type(original_update)~='function' then return false end
    env.update=function(w,h,ticks,...)
        local reset=banner();if reset then reset_banner=reset end
        if reset_banner then reset_banner() end
        local result=original_update(w,h,ticks,...)
        -- begin() may have replaced the menu UI during original_update.
        local reset_after=banner();if reset_after then reset_banner=reset_after end
        if not active() or not ensure_ui() then return result end
        ui:begin_ui(ticks)
        local width=math.min(430,w-24)
        ui:begin_window('CC2X SDK UPDATE AVAILABLE',12,12,width,h-24,
            env.atlas_icons and env.atlas_icons.hud_warning or 0,true)
        ui:header('UPDATE YOUR INSTALLED DLL')
        ui:text_basic('Installed: '..tostring(installed or 'UNKNOWN'),env.color_status_bad or env.color_white)
        ui:text_basic('Available: '..latest,env.color_white)
        ui:text_basic('Steam updates the download, not your installed SDK.',env.color_white)
        ui:spacer(4)
        if instructions then
            ui:text_basic('1. Close Carrier Command 2.',env.color_white)
            ui:text_basic('2. Open CC2ModLoader.exe (beta 12 or newer).',env.color_white)
            ui:text_basic('3. Let your selected mods apply, then Launch.',env.color_white)
            ui:spacer(4)
            ui:text_basic('Manual: replace the recognized CC2X dinput8.dll',env.color_white)
            ui:text_basic('beside carrier_command.exe with the SDK download.',env.color_white)
            ui:text_basic('Back up the old DLL; preserve unrelated loaders.',env.color_white)
        elseif ui:button('HOW TO UPDATE',true) then instructions=true end
        ui:spacer(4)
        ui:text_basic('Existing compatible features can still be used.',env.color_white)
        if ui:button('CONTINUE WITH OLD SDK',true) then modal=false;instructions=false end
        ui:end_window();ui:end_ui()
        return result
    end
    for _,name in ipairs({'input_event','input_pointer','input_scroll','input_axis','input_text'})do
        local original=env[name]
        env[name]=function(...)
            if active() and ensure_ui() then
                if name=='input_axis' then
                    local _,_,_,scroll=...
                    if type(env.is_gamepad_scroll)=='function' and env.is_gamepad_scroll()then ui:input_scroll_gamepad(scroll)end
                elseif type(ui[name])=='function' then ui[name](ui,...) end
                return
            end
            if original then return original(...)end
        end
    end
    if type(env.cc2x)=='table' and type(env.cc2x.log)=='function' then
        env.cc2x.log('SDK_UPDATE_NOTICE installed='..tostring(installed)..' available='..latest)
    end
    return true
end
