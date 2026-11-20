local cases=0
local function fixture(version)
 local t={labels={},click=nil,inputs=0,updates=0,logs={}}
 local function label(_,text)t.labels[#t.labels+1]=text end
 local function create()
  return {begin_ui=function()end,end_ui=function()end,begin_window=label,end_window=function()end,
   header=label,text_basic=label,spacer=function()end,button=function(_,text)
    label(nil,text);if t.click==text then t.click=nil;return true end;return false end,
   input_event=function()t.modal_input=true end}
 end
 local e={cc2x={version=version,log=function(s)t.logs[#t.logs+1]=s end},g_screens={main=0},g_screen_index=0,
  lib_imgui={create_ui=create},atlas_icons={},color_white={},g_ui=create(),nav_set_screen=function()end}
 e.update=function()t.updates=t.updates+1;e.g_ui:header('GAME');return 17 end
 e.input_event=function()t.inputs=t.inputs+1;return 9 end
 function t:shown(text)for _,s in ipairs(self.labels)do if s==text then return true end end;return false end
 return e,t
end
for _,v in ipairs({'0.11.0-dev','0.12.0-dev','1.0.0'})do
 local e,t=fixture(v);assert(not POLICY(e,'0.11.0-dev'))
 assert(e.update(800,600,1)==17 and not t:shown('UPDATE YOUR INSTALLED DLL'))
 assert(e.input_event(1,1)==9);cases=cases+1
end
for _,v in ipairs({'0.10.0-dev','0.8.1-client-beta.1','unknown'})do
 local e,t=fixture(v);assert(POLICY(e,'0.11.0-dev'));assert(not POLICY(e,'0.11.0-dev'))
 assert(e.update(800,600,1)==17 and t:shown('UPDATE YOUR INSTALLED DLL'))
 e.input_event(1,1);assert(t.modal_input and t.inputs==0)
 t.click='HOW TO UPDATE';e.update(800,600,1);e.update(800,600,1)
 assert(t:shown('1. Close Carrier Command 2.'))
 t.click='CONTINUE WITH OLD SDK';e.update(800,600,1)
 assert(e.input_event(1,1)==9 and t.inputs==1)
 t.labels={};e.update(800,600,1);assert(t:shown('CC2X SDK UPDATE AVAILABLE'))
 t.labels={};e.update(800,600,1);assert(t:shown('CC2X SDK UPDATE AVAILABLE'))
 e.g_screen_index=4;t.labels={};e.update(800,600,1);assert(not t:shown('CC2X SDK UPDATE AVAILABLE'))
 cases=cases+1
end
do
 local e,t=fixture('0.10.0-dev');e.g_screens=nil
 assert(not POLICY(e,'0.11.0-dev'));assert(e.input_event(1,1)==9)
 assert(not e.__cc2x_update_notice);cases=cases+1
end
print('PASS SDK update notice '..cases..' real-Lua scenarios')
