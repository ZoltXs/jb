/*
 * Driver simple para display LPM027M128C en Raspberry Pi
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>

/* Definiciones básicas */
#define LPM027M128C_WIDTH  400
#define LPM027M128C_HEIGHT 240
#define LPM027M128C_BPP    32  /* Usamos 32bpp para simplificar */

/* Modos de operación del display */
#define LPM027M128C_MODE_ALL_CLEAR            0x08
#define LPM027M128C_MODE_UPDATE_SINGLE_3BIT   0x01

/* Estructura para mantener el estado del display */
struct lpm027m128c_par {
    struct spi_device *spi;
    struct fb_info *info;
    struct gpio_desc *gpio_scs;      /* Chip Select Signal */
    struct gpio_desc *gpio_extcomin; /* COM Inversion Signal Input */
    struct gpio_desc *gpio_disp;     /* Display ON/OFF Switching Signal */
    struct gpio_desc *gpio_extmode;  /* COM Inversion Mode Select Terminal */
    u32 *vmem;                       /* Virtual memory buffer */
    bool display_on;                 /* Estado del display (on/off) */
    struct mutex lock;               /* Mutex para proteger el acceso */
};

/* Función para enviar comandos al display */
static int lpm027m128c_write_cmd(struct lpm027m128c_par *par, u8 cmd)
{
    struct spi_transfer t = {
        .tx_buf = &cmd,
        .len = 1,
        .speed_hz = 1000000, /* 1MHz - velocidad segura */
    };
    struct spi_message m;
    
    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    
    return spi_sync(par->spi, &m);
}

/* Función para enviar datos al display */
static int lpm027m128c_write_data(struct lpm027m128c_par *par, u8 *data, size_t len)
{
    struct spi_transfer t = {
        .tx_buf = data,
        .len = len,
        .speed_hz = 1000000, /* 1MHz - velocidad segura */
    };
    struct spi_message m;
    
    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    
    return spi_sync(par->spi, &m);
}

/* Función para inicializar el display */
static int lpm027m128c_init_display(struct lpm027m128c_par *par)
{
    int ret;

    /* Configurar pines GPIO */
    gpiod_set_value_cansleep(par->gpio_extmode, 0); /* EXTMODE = L */
    gpiod_set_value_cansleep(par->gpio_disp, 0);    /* Display off inicialmente */
    
    /* Esperar a que el display se estabilice */
    msleep(10);
    
    /* Activar el display */
    gpiod_set_value_cansleep(par->gpio_disp, 1);
    
    /* Enviar comando de limpieza (All Clear) */
    gpiod_set_value_cansleep(par->gpio_scs, 1);
    ret = lpm027m128c_write_cmd(par, LPM027M128C_MODE_ALL_CLEAR);
    gpiod_set_value_cansleep(par->gpio_scs, 0);
    
    if (ret < 0)
        return ret;
    
    /* Esperar a que se complete la limpieza */
    msleep(50);
    
    par->display_on = true;
    
    return 0;
}

/* Función para actualizar una línea del display */
static int lpm027m128c_update_line(struct lpm027m128c_par *par, int line)
{
    u8 cmd_buf[7];
    u8 *line_buf;
    int ret, i;
    u32 *vmem_line;
    
    if (line >= LPM027M128C_HEIGHT)
        return -EINVAL;
    
    /* Asignar buffer para datos de línea */
    line_buf = kmalloc(LPM027M128C_WIDTH * 3 / 8 + 2, GFP_KERNEL);
    if (!line_buf)
        return -ENOMEM;
    
    /* Preparar comando para actualización de línea única (3-bit mode) */
    cmd_buf[0] = 0x01; /* M0 = 1 (data update mode) */
    cmd_buf[1] = 0x00; /* M1 = 0 (COM = L) */
    cmd_buf[2] = 0x00; /* M2 = 0 (data update mode) */
    cmd_buf[3] = 0x00; /* M3-M4 = 00 (3-bit data mode) */
    cmd_buf[4] = 0x00; /* M5 = 0 (invalid data) */
    
    /* Dirección de línea (10 bits) */
    cmd_buf[5] = (line >> 8) & 0x03; /* AG9-AG8 */
    cmd_buf[6] = line & 0xFF;        /* AG7-AG0 */
    
    /* Obtener puntero a la línea en memoria virtual */
    vmem_line = par->vmem + (line * LPM027M128C_WIDTH);
    
    /* Convertir datos de framebuffer a formato del display (3 bits por píxel RGB) */
    memset(line_buf, 0, LPM027M128C_WIDTH * 3 / 8);
    for (i = 0; i < LPM027M128C_WIDTH; i++) {
        u32 pixel = vmem_line[i];
        u8 r = (pixel >> 16) & 0x01; /* Bit rojo (simplificado a 1 bit) */
        u8 g = (pixel >> 8) & 0x01;  /* Bit verde (simplificado a 1 bit) */
        u8 b = pixel & 0x01;         /* Bit azul (simplificado a 1 bit) */
        
        /* Empaquetar bits RGB en el buffer de línea */
        int byte_pos = i * 3 / 8;
        int bit_pos = (i * 3) % 8;
        
        if (bit_pos <= 5) {
            line_buf[byte_pos] |= (r << (7 - bit_pos));
            line_buf[byte_pos] |= (g << (6 - bit_pos));
            line_buf[byte_pos] |= (b << (5 - bit_pos));
        } else {
            line_buf[byte_pos] |= (r << (7 - bit_pos));
            line_buf[byte_pos] |= (g << (6 - bit_pos));
            line_buf[byte_pos + 1] |= (b << (7 - (bit_pos - 6)));
        }
    }
    
    /* Añadir 16 clocks dummy al final */
    memset(line_buf + (LPM027M128C_WIDTH * 3 / 8), 0, 2);
    
    /* Enviar comando */
    gpiod_set_value_cansleep(par->gpio_scs, 1);
    ret = lpm027m128c_write_data(par, cmd_buf, 7);
    if (ret < 0) {
        kfree(line_buf);
        return ret;
    }
    
    /* Enviar datos de línea */
    ret = lpm027m128c_write_data(par, line_buf, LPM027M128C_WIDTH * 3 / 8 + 2);
    gpiod_set_value_cansleep(par->gpio_scs, 0);
    
    kfree(line_buf);
    return ret;
}

/* Función para actualizar todo el display */
static void lpm027m128c_update_display(struct lpm027m128c_par *par)
{
    int i;
    
    mutex_lock(&par->lock);
    
    for (i = 0; i < LPM027M128C_HEIGHT; i++) {
        lpm027m128c_update_line(par, i);
        /* Pequeña pausa entre líneas */
        udelay(50);
    }
    
    mutex_unlock(&par->lock);
}

/* Callback para mmap del framebuffer */
static int lpm027m128c_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
    struct lpm027m128c_par *par = info->par;
    unsigned long start = (unsigned long)par->vmem;
    unsigned long size = info->fix.smem_len;
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long page, pos;

    if (offset >= size || (vma->vm_end - vma->vm_start + offset) > size)
        return -EINVAL;

    pos = (unsigned long)par->vmem + offset;
    
    while (size > 0) {
        page = vmalloc_to_pfn((void *)pos);
        if (remap_pfn_range(vma, vma->vm_start, page, PAGE_SIZE, vma->vm_page_prot))
            return -EAGAIN;
            
        pos += PAGE_SIZE;
        vma->vm_start += PAGE_SIZE;
        
        if (size > PAGE_SIZE)
            size -= PAGE_SIZE;
        else
            size = 0;
    }
    
    return 0;
}

/* Callback para cambiar la configuración del framebuffer */
static int lpm027m128c_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    /* Limitar a las capacidades del hardware */
    if (var->xres != LPM027M128C_WIDTH)
        var->xres = LPM027M128C_WIDTH;
    if (var->yres != LPM027M128C_HEIGHT)
        var->yres = LPM027M128C_HEIGHT;
    
    var->xres_virtual = var->xres;
    var->yres_virtual = var->yres;
    
    if (var->bits_per_pixel != 32)
        var->bits_per_pixel = 32;
    
    /* Configurar formato de color */
    var->red.offset = 16;
    var->red.length = 8;
    var->green.offset = 8;
    var->green.length = 8;
    var->blue.offset = 0;
    var->blue.length = 8;
    var->transp.offset = 24;
    var->transp.length = 8;
    
    return 0;
}

/* Callback para aplicar cambios en la configuración del framebuffer */
static int lpm027m128c_fb_set_par(struct fb_info *info)
{
    struct lpm027m128c_par *par = info->par;
    
    /* Actualizar parámetros del framebuffer */
    info->fix.line_length = info->var.xres * (info->var.bits_per_pixel / 8);
    
    /* Actualizar todo el display */
    lpm027m128c_update_display(par);
    
    return 0;
}

/* Callback para operaciones de blank (apagar/encender display) */
static int lpm027m128c_fb_blank(int blank, struct fb_info *info)
{
    struct lpm027m128c_par *par = info->par;
    
    switch (blank) {
    case FB_BLANK_UNBLANK:
        /* Encender display */
        gpiod_set_value_cansleep(par->gpio_disp, 1);
        par->display_on = true;
        break;
    case FB_BLANK_NORMAL:
    case FB_BLANK_VSYNC_SUSPEND:
    case FB_BLANK_HSYNC_SUSPEND:
    case FB_BLANK_POWERDOWN:
        /* Apagar display */
        gpiod_set_value_cansleep(par->gpio_disp, 0);
        par->display_on = false;
        break;
    default:
        return -EINVAL;
    }
    
    return 0;
}

/* Callback para escribir en el framebuffer */
static ssize_t lpm027m128c_fb_write(struct fb_info *info, const char __user *buf,
                                   size_t count, loff_t *ppos)
{
    struct lpm027m128c_par *par = info->par;
    unsigned long p = *ppos;
    void *dst;
    int err = 0;
    unsigned long total_size;
    
    if (info->state != FBINFO_STATE_RUNNING)
        return -EPERM;
    
    total_size = info->fix.smem_len;
    
    if (p > total_size)
        return -EFBIG;
    
    if (count > total_size - p)
        count = total_size - p;
    
    if (!count)
        return 0;
    
    dst = (void __force *)(info->screen_base + p);
    
    if (copy_from_user(dst, buf, count))
        err = -EFAULT;
    else {
        *ppos += count;
        
        /* Actualizar todo el display */
        lpm027m128c_update_display(par);
    }
    
    return (err) ? err : count;
}

/* Callback para operaciones de fillrect */
static void lpm027m128c_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
    struct lpm027m128c_par *par = info->par;
    sys_fillrect(info, rect);
    
    /* Actualizar todo el display */
    lpm027m128c_update_display(par);
}

/* Callback para operaciones de copyarea */
static void lpm027m128c_fb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
    struct lpm027m128c_par *par = info->par;
    sys_copyarea(info, area);
    
    /* Actualizar todo el display */
    lpm027m128c_update_display(par);
}

/* Callback para operaciones de imageblit */
static void lpm027m128c_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
    struct lpm027m128c_par *par = info->par;
    sys_imageblit(info, image);
    
    /* Actualizar todo el display */
    lpm027m128c_update_display(par);
}

/* Operaciones del framebuffer */
static struct fb_ops lpm027m128c_ops = {
    .owner = THIS_MODULE,
    .fb_read = fb_sys_read,
    .fb_write = lpm027m128c_fb_write,
    .fb_fillrect = lpm027m128c_fb_fillrect,
    .fb_copyarea = lpm027m128c_fb_copyarea,
    .fb_imageblit = lpm027m128c_fb_imageblit,
    .fb_mmap = lpm027m128c_fb_mmap,
    .fb_check_var = lpm027m128c_fb_check_var,
    .fb_set_par = lpm027m128c_fb_set_par,
    .fb_blank = lpm027m128c_fb_blank,
};

/* Función para configurar el framebuffer */
static int lpm027m128c_fb_setup(struct lpm027m128c_par *par)
{
    struct fb_info *info;
    int retval = -ENOMEM;
    
    info = framebuffer_alloc(0, &par->spi->dev);
    if (!info)
        return -ENOMEM;
    
    par->info = info;
    info->par = par;
    info->fbops = &lpm027m128c_ops;
    info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_NONE;
    
    /* Configurar parámetros del framebuffer */
    info->fix.type = FB_TYPE_PACKED_PIXELS;
    info->fix.visual = FB_VISUAL_TRUECOLOR;
    info->fix.xpanstep = 0;
    info->fix.ypanstep = 0;
    info->fix.ywrapstep = 0;
    info->fix.line_length = LPM027M128C_WIDTH * 4; /* 32bpp = 4 bytes por píxel */
    info->fix.accel = FB_ACCEL_NONE;
    strcpy(info->fix.id, "lpm027m128c");
    
    info->var.xres = LPM027M128C_WIDTH;
    info->var.yres = LPM027M128C_HEIGHT;
    info->var.xres_virtual = info->var.xres;
    info->var.yres_virtual = info->var.yres;
    info->var.bits_per_pixel = 32; /* Usamos 32bpp para simplificar */
    info->var.activate = FB_ACTIVATE_NOW;
    info->var.height = -1;
    info->var.width = -1;
    
    /* Configurar formato de color */
    info->var.red.offset = 16;
    info->var.red.length = 8;
    info->var.green.offset = 8;
    info->var.green.length = 8;
    info->var.blue.offset = 0;
    info->var.blue.length = 8;
    info->var.transp.offset = 24;
    info->var.transp.length = 8;
    
    /* Asignar memoria para el framebuffer */
    info->screen_size = info->fix.line_length * info->var.yres;
    info->fix.smem_len = info->screen_size;
    
    par->vmem = vmalloc(info->fix.smem_len);
    if (!par->vmem) {
        framebuffer_release(info);
        return -ENOMEM;
    }
    
    memset(par->vmem, 0, info->fix.smem_len);
    info->screen_base = (char __iomem *)par->vmem;
    
    /* Registrar el framebuffer */
    retval = register_framebuffer(info);
    if (retval < 0) {
        dev_err(&par->spi->dev, "Error al registrar framebuffer: %d\n", retval);
        vfree(par->vmem);
        framebuffer_release(info);
        return retval;
    }
    
    dev_info(&par->spi->dev, "Framebuffer %s registrado, %dx%d, %d bytes de memoria\n",
             info->fix.id, info->var.xres, info->var.yres, info->fix.smem_len);
    
    return 0;
}

/* Función para liberar recursos del framebuffer */
static void lpm027m128c_fb_destroy(struct lpm027m128c_par *par)
{
    if (par->info) {
        unregister_framebuffer(par->info);
        vfree(par->vmem);
        framebuffer_release(par->info);
    }
}

/* Función para sondear el dispositivo */
static int lpm027m128c_probe(struct spi_device *spi)
{
    struct lpm027m128c_par *par;
    struct device *dev = &spi->dev;
    int ret;
    
    /* Configurar SPI */
    spi->bits_per_word = 8;
    spi->mode = SPI_MODE_0;
    spi->max_speed_hz = 1000000; /* 1MHz - velocidad segura */
    ret = spi_setup(spi);
    if (ret < 0) {
        dev_err(dev, "Error al configurar SPI: %d\n", ret);
        return ret;
    }
    
    /* Asignar memoria para la estructura de datos privada */
    par = devm_kzalloc(dev, sizeof(struct lpm027m128c_par), GFP_KERNEL);
    if (!par)
        return -ENOMEM;
    
    par->spi = spi;
    spi_set_drvdata(spi, par);
    
    /* Obtener pines GPIO desde device tree */
    par->gpio_scs = devm_gpiod_get(dev, "scs", GPIOD_OUT_LOW);
    if (IS_ERR(par->gpio_scs)) {
        ret = PTR_ERR(par->gpio_scs);
        dev_err(dev, "Error al obtener GPIO SCS: %d\n", ret);
        return ret;
    }
    
    par->gpio_extcomin = devm_gpiod_get(dev, "extcomin", GPIOD_OUT_LOW);
    if (IS_ERR(par->gpio_extcomin)) {
        ret = PTR_ERR(par->gpio_extcomin);
        dev_err(dev, "Error al obtener GPIO EXTCOMIN: %d\n", ret);
        return ret;
    }
    
    par->gpio_disp = devm_gpiod_get(dev, "disp", GPIOD_OUT_LOW);
    if (IS_ERR(par->gpio_disp)) {
        ret = PTR_ERR(par->gpio_disp);
        dev_err(dev, "Error al obtener GPIO DISP: %d\n", ret);
        return ret;
    }
    
    par->gpio_extmode = devm_gpiod_get(dev, "extmode", GPIOD_OUT_LOW);
    if (IS_ERR(par->gpio_extmode)) {
        ret = PTR_ERR(par->gpio_extmode);
        dev_err(dev, "Error al obtener GPIO EXTMODE: %d\n", ret);
        return ret;
    }
    
    /* Inicializar mutex */
    mutex_init(&par->lock);
    
    /* Inicializar el display */
    ret = lpm027m128c_init_display(par);
    if (ret < 0) {
        dev_err(dev, "Error al inicializar display: %d\n", ret);
        return ret;
    }
    
    /* Configurar framebuffer */
    ret = lpm027m128c_fb_setup(par);
    if (ret < 0) {
        dev_err(dev, "Error al configurar framebuffer: %d\n", ret);
        return ret;
    }
    
    dev_info(dev, "Driver LPM027M128C para Raspberry Pi inicializado correctamente\n");
    
    return 0;
}

/* Función para remover el dispositivo */
static int lpm027m128c_remove(struct spi_device *spi)
{
    struct lpm027m128c_par *par = spi_get_drvdata(spi);
    
    /* Apagar display */
    gpiod_set_value_cansleep(par->gpio_disp, 0);
    
    /* Liberar recursos */
    lpm027m128c_fb_destroy(par);
    
    return 0;
}

/* Tabla de compatibilidad para Device Tree */
static const struct of_device_id lpm027m128c_of_match[] = {
    { .compatible = "japan-display,lpm027m128c" },
    { }
};
MODULE_DEVICE_TABLE(of, lpm027m128c_of_match);

/* Tabla de dispositivos SPI */
static const struct spi_device_id lpm027m128c_id[] = {
    { "lpm027m128c", 0 },
    { }
};
MODULE_DEVICE_TABLE(spi, lpm027m128c_id);

/* Driver SPI */
static struct spi_driver lpm027m128c_driver = {
    .driver = {
        .name = "lpm027m128c_fb",
        .of_match_table = lpm027m128c_of_match,
    },
    .id_table = lpm027m128c_id,
    .probe = lpm027m128c_probe,
    .remove = lpm027m128c_remove,
};

module_spi_driver(lpm027m128c_driver);

MODULE_DESCRIPTION("Framebuffer driver simple para LPM027M128C display en Raspberry Pi");
MODULE_AUTHOR("Your Name");
MODULE_LICENSE("GPL v2");
