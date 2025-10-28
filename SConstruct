#!/usr/bin/env python
import os
import sys


def normalize_path(val, env):
    return val if os.path.isabs(val) else os.path.join(env.Dir("#").abspath, val)


def validate_parent_dir(key, val, env):
    if not os.path.isdir(normalize_path(os.path.dirname(val), env)):
        raise UserError("'%s' is not a directory: %s" % (key, os.path.dirname(val)))


def set_build_info(source, target):
    with open(source, 'r') as f:
        content = f.read()

    content = content.replace("${version}", os.environ.get('TAG_VERSION', '0.0.0'))
    content = content.replace("${build}", os.environ.get('BUILD_SHA', 'none'))

    with open(target, 'w') as f:
        f.write(content) 


build_info_source_files = ["templates/godotlv2host.gdextension.in",
                           "templates/version_generated.gen.h.in"]

build_info_target_files = ["addons/lv2-host/bin/godotlv2host.gdextension",
                           "src/version_generated.gen.h"]


def create_build_info_files(target, source, env):
    for i in range(len(build_info_source_files)):
        set_build_info(build_info_source_files[i], build_info_target_files[i])


def create_build_info_strfunction(target, source, env):
    return f"Creating build info files: {', '.join(str(t) for t in target)}"


libname = "liblv2hostgodot"
projectdir = "."

if sys.platform == "windows":
    localEnv = Environment(tools=["mingw"], PLATFORM="")
else:
    localEnv = Environment(tools=["default"], PLATFORM="")

customs = ["custom.py"]
customs = [os.path.abspath(path) for path in customs]

opts = Variables(customs, ARGUMENTS)
opts.Add(
    BoolVariable(
        key="compiledb",
        help="Generate compilation DB (`compile_commands.json`) for external tools",
        default=localEnv.get("compiledb", False),
    )
)
opts.Add(
    PathVariable(
        key="compiledb_file",
        help="Path to a custom `compile_commands.json` file",
        default=localEnv.get("compiledb_file", "compile_commands.json"),
        validator=validate_parent_dir,
    )
)
opts.Update(localEnv)

Help(opts.GenerateHelpText(localEnv))

env = localEnv.Clone()
env["compiledb"] = False

env.Tool("compilation_db")
compilation_db = env.CompilationDatabase(
    normalize_path(localEnv["compiledb_file"], localEnv)
)
env.Alias("compiledb", compilation_db)

env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})

if env["platform"] == "windows":
    env.Append(CPPFLAGS=["-DMINGW"])
elif env["platform"] == "web":
    if env["dev_build"]:
        env.Append(CPPFLAGS=["-g"])
        env.Append(LINKFLAGS=["-g", "-s", "ERROR_ON_UNDEFINED_SYMBOLS=1"])
elif env["platform"] == "macos":
    lilv_library = "lilv-0"
    env.Append(LIBS=[lilv_library, "serd-0", "sord-0", "zix-0", "sratom-0"])

    if env["dev_build"]:
        env.Append(LIBPATH=["addons/lv2-host/bin/osxcross/debug/vcpkg_installed/univeral-osxcross/lib"])
        env.Append(CPPPATH=["addons/lv2-host/bin/osxcross/debug/vcpkg_installed/univeral-osxcross/include"])
        #env.Append(RPATH=["", "."])
    else:
        env.Append(LIBPATH=["addons/lv2-host/bin/osxcross/release/vcpkg_installed/univeral-osxcross/lib"])
        env.Append(CPPPATH=["addons/lv2-host/bin/osxcross/release/vcpkg_installed/univeral-osxcross/include"])
        #env.Append(RPATH=["", "."])
elif env["platform"] == "linux":
    lilv_library = "lilv-0"
    env.Append(LIBS=[lilv_library, "serd-0", "sord-0", "zix-0", "sratom-0"])

    if env["dev_build"]:
        env.Append(LIBPATH=["addons/lv2-host/bin/linux/debug/vcpkg_installed/x64-linux/lib"])
        env.Append(CPPPATH=["addons/lv2-host/bin/linux/debug/vcpkg_installed/x64-linux/include", "addons/lv2-host/bin/linux/debug/vcpkg_installed/x64-linux/include/lilv-0"])
        #env.Append(RPATH=["", "."])
    else:
        env.Append(LIBPATH=["addons/lv2-host/bin/linux/release/vcpkg_installed/x64-linux/lib"])
        env.Append(CPPPATH=["addons/lv2-host/bin/linux/release/vcpkg_installed/x64-linux/include", "addons/lv2-host/bin/linux/release/vcpkg_installed/x64-linux/include/lilv-0"])
        #env.Append(RPATH=["", "."])

#env.Append(CPPFLAGS=["-fexceptions"])

env.Append(CPPPATH=["src/"])
sources = Glob("src/*.cpp")

if env["target"] in ["editor", "template_debug"]:
	try:
		doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
		sources.append(doc_data)
	except AttributeError:
		print("Not including class reference as we're targeting a pre-4.3 baseline.")

file = "{}{}{}".format(libname, env["suffix"], env["SHLIBSUFFIX"])

if env["platform"] == "macos":
    platlibname = "{}.{}.{}".format(libname, env["platform"], env["target"])
    file = "{}.framework/{}".format(env["platform"], platlibname, platlibname)

libraryfile = "addons/lv2-host/bin/{}/{}".format(env["platform"], file)
library = env.SharedLibrary(
    libraryfile,
    source=sources,
)

create_build_info_action = Action(create_build_info_files,
                                  strfunction=create_build_info_strfunction)

env.Command(
    build_info_target_files,
    build_info_source_files,
    create_build_info_action
)

env.AddPreAction(library, create_build_info_action)
env.Depends(library, build_info_target_files)

#copy = env.InstallAs("{}/bin/{}/lib{}".format(projectdir, env["platform"], file), library)

default_args = [library]
if localEnv.get("compiledb", False):
    default_args += [compilation_db]
Default(*default_args)

