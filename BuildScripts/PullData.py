import requests
import os
import random
import secrets
import base64
import json
import datetime
import sys

header = {"Content-type": "application/json"} 

def DownloadData(url, dataType, sDate, machineCredsB64, projectCredsB64, Platform, ShaderModel, ext=""):
    global header
    requestData = {
        "date": sDate,
        "machine": machineCredsB64,
        "project": projectCredsB64,
        "platform": Platform,
        "shadermodel": ShaderModel,
        "type": dataType
    }

    if len(ext) == 0:
        ext = dataType

    p = requests.post(url, data=json.dumps(requestData), headers=header)

    ### Pull ShaderPipelines
    print(p.status_code)
    if p.status_code == 200:
        jsonblob = p.json()
        index = 0

        print("Fetching {} items".format(len(jsonblob)))

        for shader in jsonblob:
            
            print("{} -> V{}.{}.{}.{}_{}.{}".format(index, shader["versionmajor"], shader["versionminor"], shader["versionrevision"], shader["versionbuild"], index, ext))
            filename = "V{}.{}.{}.{}_{}.{}".format(shader["versionmajor"], shader["versionminor"], shader["versionrevision"], shader["versionbuild"], index, ext)
            index += 1

            if (dataType == "pipelinecache"):
                data = base64.b64decode(shader["pipelinecachedata"])
            elif (dataType == "shk"):
                data = base64.b64decode(shader["stablekeyinfodata"])
            else:
                return -1

            with open(os.path.join(OutDirectory, filename), "wb") as f:
                f.write(data)

        return 0



if len(sys.argv) != 6:
    print("Incorrect number of args: <Platform> <ShaderModel> <OutDirectory> <MachineCredentialFile> <ProjectCredentialFile>")

Platform = sys.argv[1]
ShaderModel = sys.argv[2]
OutDirectory = sys.argv[3]
MachineCredentialFile = sys.argv[4]
ProjectCredentialFile = sys.argv[5]

# Linux PCD3D_SM5 \"${WORKSPACE}/PipelineBuilds/PCD3D_SM5\" \"${PullMachineCreds}\""

dNow = datetime.datetime.now()
sDate = str(dNow - datetime.timedelta(days=14))

print("Fetching before {}".format(sDate))

uploadURL = "/api/pco/date/after/"

# machineCredsB64 = ""
# projectCredsB64 = ""

# with open(MachineCredentialFile, "r") as f:
#     machineCredsB64 = f.readline()

# with open(ProjectCredentialFile, "r") as f:
#     projectCredsB64 = f.readline()


rootUrl = "https://<domain>" + uploadURL

retVal = DownloadData(rootUrl, "pipelinecache", sDate, MachineCredentialFile, ProjectCredentialFile, Platform, ShaderModel, "upipelinecache")
if (0 != retVal):
    exit(retVal)

retVal = DownloadData(rootUrl, "shk", sDate, MachineCredentialFile, ProjectCredentialFile, Platform, ShaderModel)
if (0 != retVal):
    exit(retVal)