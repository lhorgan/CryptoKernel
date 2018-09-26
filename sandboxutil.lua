function ck_tostring(v)
    if type(v) == "number" 
    or type(v) == "boolean"
    or type(v) == "string" then
        return tostring(v)
    elseif type(v) == "table" then
        if type(v["__tostring"]) == "function" then
            return v["tostring"](v)
        else
            return "table"
        end
    else
        return type(v)
    end
end