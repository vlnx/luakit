--- Unique instance support for luakit.
--
-- This module provides a simple implementation of unique instances.
-- With this module enabled, only one instance of luakit will be run;
-- opening links from other programs or from the command line will open
-- those links in an already-running instance of luakit.
--
-- This module should be the first module loaded in your configuration file.
--
-- @module unique_instance
-- @copyright 2017 Aidan Holm <aidanholm@gmail.com>

local _M = {}

-- Check that this module is loaded first: simple but effective
for _, k in ipairs({"window", "webview", "lousy", "globals"}) do
    assert(not package.loaded[k], "unique_instance should be loaded before all other modules!")
end

local lfs = require "lfs"
local unique = luakit.unique

--- Whether links from secondary luakit instances should open in a new
-- window; if `true`, links will be opened in a new window, if `false`,
-- links will be opened in an existing luakit window.
-- @type boolean
-- @readwrite
-- @default `false`
_M.open_links_in_new_window = false

if not unique then
    msg.verbose("luakit started with no-unique")
    return _M
end

unique.new()

-- Check for a running luakit instance
if unique.is_running() then
    msg.verbose("a primary instance is already running")
    local pickle = require("lousy.pickle")

    -- local u = {}
    -- for i, uri in ipairs(uris) do
    --     u[i] = lfs.attributes(uri) and ("file://"..os.abspath(uri):gsub(" ","%%20")) or uri
    -- end

    unique.send_open_signal("deprecated")
    luakit.quit()
end

return _M

-- vim: et:sw=4:ts=8:sts=4:tw=80
