-- Owned diagnostic fixture appended to game UI through the production API.
if type(cc2x) == "table" and type(cc2x.log) == "function" then
    local safe = dofile == nil and loadfile == nil and load == nil and loadstring == nil
    cc2x.log("[game1520] " .. (safe and "PASS" or "FAIL") ..
        " screen extension; security cleanup retained; version=" .. tostring(cc2x.version))
end
