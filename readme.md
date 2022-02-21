# SolARUnityClientHololens2Plugin

[![License](https://img.shields.io/github/license/SolARFramework/SolARUnityPlugin?style=flat-square&label=License)](https://www.apache.org/licenses/LICENSE-2.0)

## Description
This project is a C++ library which retrieves Hololens 2 sensor data: images, poses and timestamp from the cameras. It also provides a C# binding which makes it usable in Unity.

This project is based on [HoloLens2-ResearchMode-Unity](https://github.com/petergu684/HoloLens2-ResearchMode-Unity) (MIT license), and [HoloLens2ForCV StreamRecorderApp](https://github.com/microsoft/HoloLens2ForCV/tree/main/Samples/StreamRecorder/StreamRecorderApp) (MIT license)

## How to use in Unity
### Build native plugin and import to Unity
* Build this project (ARM64,Release) and copy the .dll and .winmd files into Assets/Plugins/WSA/ARM64 folder of your Unity project.
* Change the architecture in your Unity build settings to be ARM64.
* After building the visual studio solution from Unity, go to `App/[Project name]/Package.appxmanifest` and add the restricted capability to the manifest file.
```xml
<?xml version="1.0" encoding="utf-8"?>
<Package 
...
xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities" 
...
IgnorableNamespaces="... rescap ..." 
...
>
... 
<Capabilities>
...
<rescap:Capability Name="perceptionSensorsExperimental" />
<DeviceCapability Name="webcam" />
...
</Capabilities>
...
</Package>
```
* Save the changes and deploy the solution to your HoloLens 2.

### Use in your app
* Instanciate the `SolARHololens2ResearchMode` object by calling its default constructor.
* Start by calling the `Enable*()` methods corresponding to the sensors to be used and then `Init()`, in your `Start()` method for example
* Call `Update()` periodically to fetch new frames
* Frame data are returned by calling the `Get*Data()` methods for each sensor.

