import os
import sys
import shutil
import subprocess

PlatformConversion = {
    "PCD3D_SM5" : "Windows",
    "PCD3D_SM6" : "Windows"
}

if len(sys.argv) != 6:
    print("Incorrect number of args: <ExePath> <ProjectName> <Platform> <PipelineDirectory> <OutDirectory>")

ExePath = sys.argv[1]
ProjectName = sys.argv[2]
Platform = sys.argv[3]
PipelineDirectory = sys.argv[4]
OutDirectory = sys.argv[5]

ProjectDeviceProfile = Platform

if Platform in PlatformConversion:
    ProjectDeviceProfile = PlatformConversion[Platform]

WritebackLocation = os.path.join(os.path.join(os.path.join(OutDirectory, "Build"), ProjectDeviceProfile), "PipelineCaches")

Command = []

FullPath = os.path.abspath(ExePath)

if os.path.exists(FullPath):
    # Append EXE
    
    Command.append(FullPath)

    ProjectFile = os.path.join(OutDirectory,"{}.uproject".format( ProjectName ))

    if os.path.exists(ProjectFile):
        Command.append(ProjectFile)
        Command.append("-run=ShaderPipelineCacheTools")
        Command.append("Expand")

        SpecificPipelineDirectory = os.path.join(PipelineDirectory, Platform)
        if not os.path.exists(SpecificPipelineDirectory):
            os.makedirs(SpecificPipelineDirectory)

        # For the pipeline cache
        # confirm we *actually* have files
        psoFiles = []
        shkFiles = []
        for r,d,p in os.walk(SpecificPipelineDirectory):
            for f in p:
                fname, fext = os.path.splitext(f)
                if fext == ".shk":
                    shkFiles.append(os.path.join(r,f))
                elif fext == ".upipelinecache":
                    psoFiles.append(os.path.join(r,f))

        if len(shkFiles) > 0 and len(psoFiles) > 0:
            # Continue
            if not os.path.exists(WritebackLocation):
                os.makedirs(WritebackLocation)

            ResultFilename = "{}_{}.spc".format(ProjectName, Platform)
            ResultName = os.path.join(WritebackLocation, ResultFilename)

            Command.append(os.path.join(SpecificPipelineDirectory, "*.upipelinecache"))
            Command.append(os.path.join(SpecificPipelineDirectory, "*.shk"))
            Command.append(ResultName)

            CommandString = ' '.join(Command)
            print("Executing '{}'".format(CommandString))

            # DANGER
            retVal = subprocess.run(Command)
            exit(retVal.returncode)
            #os.system(CommandString)

            # # Move
            # shutil.move(ResultName, WritebackLocation)
            # WritebackLocation

        else:
            print("No Files")
            exit(0)
    else:
        print("{} does not exist".format(ProjectFile))
        exit(-2)
else:
    print("{} does not exist".format(FullPath))
    exit(-1)

