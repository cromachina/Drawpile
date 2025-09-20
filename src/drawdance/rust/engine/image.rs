use crate::{
    dp_error_anyhow, DP_CanvasState, DP_Image, DP_ImageScaleInterpolation, DP_Output, DP_UPixel8,
    DP_blend_color8_background, DP_blend_color8_to, DP_canvas_state_to_flat_image,
    DP_file_output_new_from_path, DP_image_free, DP_image_height, DP_image_new,
    DP_image_new_subimage, DP_image_pixels, DP_image_scale_pixels, DP_image_width,
    DP_image_write_jpeg, DP_image_write_png, DP_image_write_qoi, DP_image_write_webp,
    DP_output_free, DP_FLAT_IMAGE_RENDER_FLAGS,
};
use anyhow::{anyhow, Result};
use core::slice;
use std::{
    ffi::{c_int, CString},
    io::{self},
    mem::size_of,
    ptr::{copy_nonoverlapping, null},
};

use super::DrawContext;

pub struct Image {
    image: *mut DP_Image,
}

impl Image {
    pub fn new(width: usize, height: usize) -> Result<Self> {
        if width > 0 && height > 0 {
            let w = c_int::try_from(width)?;
            let h = c_int::try_from(height)?;
            let image = unsafe { DP_image_new(w, h) };
            Ok(Self { image })
        } else {
            Err(anyhow!("Empty image"))
        }
    }

    pub fn new_from_pixels(width: usize, height: usize, pixels: &[u32]) -> Result<Self> {
        let count = width * height;
        if pixels.len() >= count {
            let img = Self::new(width, height)?;
            unsafe {
                copy_nonoverlapping(
                    pixels.as_ptr(),
                    DP_image_pixels(img.image).cast::<u32>(),
                    count,
                );
            }
            Ok(img)
        } else {
            Err(anyhow!("Not enough pixels"))
        }
    }

    pub fn new_from_pixels_scaled(
        width: usize,
        height: usize,
        pixels: &[u32],
        scale_width: usize,
        scale_height: usize,
        expand: bool,
        interpolation: DP_ImageScaleInterpolation,
        dc: &mut DrawContext,
    ) -> Result<Self> {
        if width == 0 || height == 0 {
            return Err(anyhow!("Empty source image"));
        }

        if scale_width == 0 || scale_height == 0 {
            return Err(anyhow!("Empty target image"));
        }

        let count = width * height;
        if pixels.len() < count {
            return Err(anyhow!("Not enough pixels"));
        }

        if width == scale_width && height == scale_height {
            let img = unsafe { DP_image_new(width as i32, height as i32) };
            unsafe {
                copy_nonoverlapping(pixels.as_ptr(), DP_image_pixels(img).cast(), width * height);
            }
            return Ok(Image { image: img });
        }

        let xratio = scale_width as f64 / width as f64;
        let yratio = scale_height as f64 / height as f64;
        let (target_width, target_height) = if (xratio - yratio).abs() < 0.01 {
            (scale_width, scale_height)
        } else if xratio <= yratio {
            (scale_width, (height as f64 * xratio) as usize)
        } else {
            ((width as f64 * yratio) as usize, scale_height)
        };

        let image = unsafe {
            DP_image_scale_pixels(
                c_int::try_from(width)?,
                c_int::try_from(height)?,
                pixels.as_ptr().cast(),
                dc.as_ptr(),
                c_int::try_from(target_width)?,
                c_int::try_from(target_height)?,
                interpolation,
            )
        };
        if image.is_null() {
            return Err(dp_error_anyhow());
        }

        let img = Image { image };
        if expand && (target_width != scale_width || target_height != scale_height) {
            let subimg = unsafe {
                DP_image_new_subimage(
                    img.image,
                    -c_int::try_from((scale_width - target_width) / 2_usize)?,
                    -c_int::try_from((scale_height - target_height) / 2_usize)?,
                    c_int::try_from(scale_width)?,
                    c_int::try_from(scale_height)?,
                )
            };
            if subimg.is_null() {
                Err(dp_error_anyhow())
            } else {
                Ok(Image { image: subimg })
            }
        } else {
            Ok(img)
        }
    }

    pub fn new_from_canvas_state(cs: *mut DP_CanvasState) -> Result<Self> {
        let image = unsafe {
            DP_canvas_state_to_flat_image(cs, DP_FLAT_IMAGE_RENDER_FLAGS, null(), null())
        };
        if image.is_null() {
            Err(dp_error_anyhow())
        } else {
            Ok(Image { image })
        }
    }

    pub fn scaled(
        &self,
        scale_width: usize,
        scale_height: usize,
        expand: bool,
        interpolation: DP_ImageScaleInterpolation,
        dc: &mut DrawContext,
    ) -> Result<Image> {
        Self::new_from_pixels_scaled(
            self.width(),
            self.height(),
            self.pixels(),
            scale_width,
            scale_height,
            expand,
            interpolation,
            dc,
        )
    }

    pub fn width(&self) -> usize {
        unsafe { DP_image_width(self.image) as usize }
    }

    pub fn height(&self) -> usize {
        unsafe { DP_image_height(self.image) as usize }
    }

    pub fn pixels(&self) -> &[u32] {
        unsafe {
            slice::from_raw_parts(
                DP_image_pixels(self.image).cast(),
                self.width() * self.height(),
            )
        }
    }

    pub fn cropped(&self, x: usize, y: usize, width: usize, height: usize) -> Result<Image> {
        let subimg = unsafe {
            DP_image_new_subimage(
                self.image,
                c_int::try_from(x)?,
                c_int::try_from(y)?,
                c_int::try_from(width)?,
                c_int::try_from(height)?,
            )
        };
        if subimg.is_null() {
            Err(dp_error_anyhow())
        } else {
            Ok(Image { image: subimg })
        }
    }

    pub fn dump(&self, writer: &mut dyn io::Write) -> io::Result<()> {
        let pixels = unsafe { DP_image_pixels(self.image) };
        let size = self.width() * self.height() * size_of::<u32>();
        writer.write_all(unsafe { slice::from_raw_parts(pixels.cast::<u8>(), size) })
    }

    pub fn write_png(&self, path: &str) -> Result<()> {
        self.write(path, DP_image_write_png)
    }

    pub fn write_jpeg(&self, path: &str) -> Result<()> {
        self.write(path, DP_image_write_jpeg)
    }

    pub fn write_qoi(&self, path: &str) -> Result<()> {
        self.write(path, DP_image_write_qoi)
    }

    pub fn write_webp(&self, path: &str) -> Result<()> {
        self.write(path, DP_image_write_webp)
    }

    fn write(
        &self,
        path: &str,
        func: unsafe extern "C" fn(*mut DP_Image, *mut DP_Output) -> bool,
    ) -> Result<()> {
        let cpath = CString::new(path)?;
        let output = unsafe { DP_file_output_new_from_path(cpath.as_ptr()) };
        if output.is_null() {
            return Err(dp_error_anyhow());
        }
        let result = if unsafe { func(self.image, output) } {
            Ok(())
        } else {
            Err(dp_error_anyhow())
        };
        unsafe { DP_output_free(output) };
        result
    }

    pub fn blend_with(&mut self, src: &Image, color: DP_UPixel8, opacity: u8) -> Result<()> {
        let w = self.width();
        let h = self.height();
        if w != src.width() || h != src.height() {
            return Err(anyhow!("Mismatched dimensions"));
        }

        unsafe {
            DP_blend_color8_to(
                DP_image_pixels(self.image),
                DP_image_pixels(src.image),
                color,
                (w * h) as c_int,
                opacity,
            );
        }
        Ok(())
    }

    pub fn add_background(&mut self, color: u32) {
        let color = DP_UPixel8 { color };
        unsafe {
            DP_blend_color8_background(
                DP_image_pixels(self.image),
                color,
                (self.width() * self.height()) as c_int,
            );
        }
    }
}

impl Drop for Image {
    fn drop(&mut self) {
        unsafe { DP_image_free(self.image) }
    }
}
