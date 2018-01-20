--- Single instance support
--
-- DOCMACRO(available:ui)
--
-- This module allows Lua code to detect an already-running instance of
-- luakit and open requested links on that instance, instead of opening
-- two separate and independent instances.
--
-- @module luakit.unique
-- @author Mason Larobina
-- @copyright 2011 Mason Larobina <mason.larobina@gmail.com>

--- @function new
-- Set up an application with a shared identifier used to locate
-- already-running instances. This must be called before calling
-- @ref{is_running} or @ref{send_open_signal}.
-- @tparam string id The application identifier string to use.

--- @function is_running
-- Check whether an application using the identifier previously given to
-- `unique.new` is already running.
-- @treturn boolean `true` if an application instance is already running.

--- @function send_open_signal
-- Send a signal to open a uri to the primary instance of
-- the application.
-- @tparam string message The message to send to the primary instance.

--- @signal open
-- Emitted only on the primary instance when a message is received from
-- a secondary instance.
-- @tparam string message The message sent with `unique.send_open_signal()`.
-- @tparam screen screen An opaque piece of data that represents the screen of
-- the currently focused window of the main instance.

-- vim: et:sw=4:ts=8:sts=4:tw=80
