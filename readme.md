# System Goal
The triangle optimizer is a hill climber for intended to allow for approximation of .bmp files using iterative addition of semi transparent geometric primitives.

# C++ implementation 
As a baseline a pure C++ implementation is created, benchmarked and optimized.

This system picks a random triangle, optimizes it to reduce the MSE between the target image and the candidate image.


## Components
### CLI definition
The tool will be called using a CLI and controlled with a config.yaml file which will allow for different parameters and tests to be completed.

```
Optimzer -i {input image path} -o {output image path} -c {config.yaml path}
```

### BMP Converter
The first stage of the system is to get .BMP files from the user. This is preformed using the std_image library.

### MSE calculation

TODO

### Renderer

TODO

# Testing and Validation

TODO
