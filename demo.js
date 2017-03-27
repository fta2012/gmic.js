const toCanvas = (imageData) => {
  const canvas = document.createElement('canvas');
  const { width, height } = imageData;
  canvas.width = width;
  canvas.height = height;
  canvas.getContext('2d').putImageData(imageData, 0, 0);
  return canvas;
};

const display = ([images, names]) => {
  const outputElement = document.getElementById('output');
  images.forEach((imageData, i) => {
    const name = document.createElement('div');
    name.textContent = names[i];
    outputElement.appendChild(name);
    outputElement.appendChild(toCanvas(imageData));
  });
};

const getMousePos = (e, canvas) => { // where e is a mouse event
  const { clientX, clientY } = e;
  const { top, right, bottom, left } = canvas.getBoundingClientRect();
  return {
    x: canvas.width * (clientX - left) / (right - left),
    y: canvas.height * (clientY - top) / (bottom - top)
  };
};

const initCanvas = (src) => {
  const originalImage = document.getElementById('inputImage');
  const maskCanvas = document.getElementById('inputMask');
  window.originalImageData = null; // Put these into global scope for debugging
  window.maskImageData = null;

  const onImageload = () => {
    const { width, height } = originalImage;
    maskCanvas.width = width;
    maskCanvas.height = height;
    const context = maskCanvas.getContext('2d');
    context.imageSmoothingEnabled = false;
    context.fillStyle = '#FF0000';
    context.lineCap = 'round';
    context.lineWidth = 50;
    context.strokeStyle = '#FF0000';
    // Temporarily draw the original image onto the maskCanvas to initialize originalImageData
    context.drawImage(originalImage, 0, 0);
    window.originalImageData = context.getImageData(0, 0, width, height);
    // Clear and redraw a red rectangle as the initial mask
    context.clearRect(0, 0, width, height);
    context.rect(width * 0.4, height * 0.4, width * 0.2, height * 0.2);
    context.fill();
    window.maskImageData = context.getImageData(0, 0, width, height);

    // Mouse event handlers
    let prevPos;
    maskCanvas.onmousedown = (e) => {
      prevPos = getMousePos(e, maskCanvas);
    };
    maskCanvas.onmouseup = () => {
      prevPos = undefined;
      window.maskImageData = context.getImageData(0, 0, width, height);
    };
    maskCanvas.onmouseleave = () => {
      prevPos = undefined;
      window.maskImageData = context.getImageData(0, 0, width, height);
    };
    maskCanvas.onmousemove = (e) => {
      if (!prevPos) {
        return;
      }
      const { x, y } = getMousePos(e, maskCanvas);
      context.beginPath();
      context.moveTo(prevPos.x, prevPos.y);
      context.lineTo(x, y);
      context.stroke();
      prevPos.x = x;
      prevPos.y = y;
    };

    const clearButton = document.getElementById('inputMaskClear');
    clearButton.onclick = () => {
      context.clearRect(0, 0, width, height);
    };
  };

  originalImage.onload = onImageload;
  originalImage.src = src;
};

document.addEventListener('DOMContentLoaded', () => {
  initCanvas('demo.jpg');

  // Reinitialize canvas on file select
  const chooser = document.getElementById('inputFile');
  chooser.onchange = (e) => {
    const reader = new FileReader();
    reader.onload = () => {
      initCanvas(reader.result);
    };
    reader.readAsDataURL(e.target.files[0]);
  };
  chooser.onclick = () => {
    chooser.value = null;
  };

  // Hook up inpaint button
  document.getElementById('inpaintButton').onclick = () => {
    const outputElement = document.getElementById('output');
    outputElement.innerHTML = '';
    setTimeout(() => {
      console.time('inpaint');
      try {
        if (GMIC.gmic) {
          let command = '--inpaint_patchmatch[0] [1] -name[-1] inpainted';
          command = command.replace(/\n/g, ' ').trim();
          display(
            GMIC.gmic(
              command,
              [window.originalImageData, window.maskImageData],
              ['original', 'mask']
            )
          );
        } else if (GMIC.inpaintPipeline) {
          // Probably using cimg.js instead of gmic.js
          const inpaintedImageData = GMIC.inpaintPipeline(window.originalImageData, window.maskImageData);
          outputElement.appendChild(toCanvas(inpaintedImageData));
        } else {
          throw new Error('GMIC not loaded');
        }
      } catch(e) {
        GMIC.printErr(e);
      }
      console.timeEnd('inpaint');
    }, 30);
  };

});
