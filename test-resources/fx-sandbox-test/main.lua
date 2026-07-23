local TAG = '[fx-sandbox-test]'

local function logResult(name, passed, detail)
    local status = passed and 'PASS' or 'FAIL'
    if detail and detail ~= '' then
        print(string.format('%s %s:%s %s', TAG, name, status, detail))
    else
        print(string.format('%s %s:%s', TAG, name, status))
    end
end

local function testFilePath()
    return string.format('%s/lua-exec-test.txt', GetResourcePath(GetCurrentResourceName()))
end

print(string.format('%s LUA_START', TAG))

local path = testFilePath()
os.remove(path)

local writeCmd = string.format("echo sandbox_lua_ok > '%s'", path)
local execResult = os.execute(writeCmd)
local execReturnedOk = execResult == true or execResult == 0

local file = io.open(path, 'r')
local fileContents = file and file:read('*a') or nil
if file then
    file:close()
end
os.remove(path)

local fileCreated = fileContents ~= nil
local fileMatches = fileContents ~= nil and string.find(fileContents, 'sandbox_lua_ok', 1, true) ~= nil
local passed = execReturnedOk and fileCreated and fileMatches

local detail = string.format(
    'exec=%s file=%s contents=%s',
    tostring(execResult),
    fileCreated and 'yes' or 'no',
    fileContents and fileContents:gsub('%s+', '') or '<missing>'
)
logResult('LUA_EXECUTE', passed, passed and '' or detail)

logResult('LUA_ALL', passed, passed and '' or 'os.execute did not create expected file')
