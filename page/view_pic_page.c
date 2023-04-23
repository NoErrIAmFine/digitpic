#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>

#include "config.h"
#include "page_manager.h"
#include "debug_manager.h"
#include "picfmt_manager.h"
#include "pic_operation.h"
#include "input_manager.h"
#include "render.h"
#include "config.h"
#include "file.h"

static struct page_struct view_pic_page;

#define MENU_ICON_HOME          0
#define MENU_ICON_GOBACK        1
#define MENU_ICON_PRE_PIC       2
#define MENU_ICON_NEXT_PIC      3
#define MENU_ICON_UNFOLD        4
#define MENU_ICON_ZOOM_IN       5
#define MENU_ICON_ZOOM_OUT      6
#define MENU_ICON_LEFT_ROTATE   7
#define MENU_ICON_RIGHT_ROTATE  8
#define MENU_ICON_PIC_RESET     9
#define MENU_ICON_FOLD          10
#define MENU_ICON_NUMS          11

#define REGION_MENU_HOME            0
#define REGION_MENU_GOBACK          1
#define REGION_MENU_PRE_PIC         2
#define REGION_MENU_NEXT_PIC        3
#define REGION_MENU_UNFOLD          4
#define REGION_MAIN_PIC             5
#define REGION_MENU_ZOOM_IN         6
#define REGION_MENU_ZOOM_OUT        7
#define REGION_MENU_LEFT_ROTATE     8
#define REGION_MENU_RIGHT_ROTATE    9
#define REGION_MENU_PIC_RESET       10

#define ZOOM_RATE (0.9)             //放大缩小的比例系数
#define MIN_DRAG_DISTANCE (4 * 4)   //能够响应的最小的拖动距离

static short min_drag_distance = MIN_DRAG_DISTANCE;

/* 图标名称数组 */
static char *menu_icon_files[MENU_ICON_NUMS] = {
    [MENU_ICON_HOME]            = "home.png",
    [MENU_ICON_GOBACK]          = "goback.png",
    [MENU_ICON_PRE_PIC]         = "pre_pic.png",
    [MENU_ICON_NEXT_PIC]        = "next_pic.png",
    [MENU_ICON_UNFOLD]          = "unfold.png",
    [MENU_ICON_ZOOM_IN]         = "zoom_in.png",
    [MENU_ICON_ZOOM_OUT]        = "zoom_out.png",
    [MENU_ICON_LEFT_ROTATE]     = "left_rotate.png",
    [MENU_ICON_RIGHT_ROTATE]    = "right_rotate.png",
    [MENU_ICON_PIC_RESET]       = "pic_reset.png",
    [MENU_ICON_FOLD]            = "fold.png",
};
static struct pixel_data menu_icon_datas[MENU_ICON_NUMS];

/* 表示展开菜单的状态，是展开还是折叠状态 */
static int menu_unfolded = 0;

/* 文件目录相关的几个全局变量 */
static char *cur_dir;                               //当前所在目录
static struct dir_entry **cur_dir_pic_contents;     //当前目录下有那些图片
static unsigned int cur_dir_pic_nums;               //当前浏览的目录下所含的图片数量
static int cur_pic_index;                           //当前正浏览的图片在目录中的索引

/* 表示一个图片缓存 */
struct pic_cache
{
    short virtual_x;        //图片在虚拟显示空间中的右上角座标，这个空间是可以超出显示屏的
    short virtual_y;
    short orig_width;       //图片原始宽度
    short orig_height;      //图片原始高度
    short angle;            //缓存中图片的角度，可能的取值:0,90,180,270
    struct pixel_data data;
    void *orig_data;
    unsigned int has_data:1;         //标志位，说明缓存中是否有数据
    unsigned int has_orig_data:1;    //标志位，表明缓存中是否含有原始数据
};
/* 缓存的图片,这里缓存的是原始数据,大小,bpp可能不同 */
static unsigned int pic_caches_generated = 0;
static struct pic_cache *pic_caches[3];
static struct pic_cache **const cur_pic_data = &pic_caches[1];  //当前的图片数据总是在第二个缓存中

/* 如果由于某些原因加载图片出错，则展示该默认图片 */
static struct pixel_data default_err_pic;

static int view_pic_page_calc_layout(struct page_struct *page)
{
    struct page_layout *layout;
    struct page_region *regions;
    unsigned int width,height;
    unsigned int x_cursor,y_cursor,unit_distance;
    unsigned int unfold_region_interval;
    unsigned int unfold_region_width;
    int i;

    width = page->page_layout.width;
    height = page->page_layout.height;
    layout = &page->page_layout;
    layout->region_num = 11;

    /* 动态分配region数组所占用的空间 */
    regions = malloc(layout->region_num * sizeof(struct page_region));
    if(!regions){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }

    layout->regions = regions;
    
    /* 一些与位置无关的成员在外面先填充了 */
    for(i = 0 ; i < 10 ; i++){
        regions[i].index = i;
        regions->file_name = menu_icon_files[i];
        if(i > 5){
            /* 这几个区域level为1(从0开始),因为它是在其他区域之上的 */
            regions->level = 1;
        }else{
            regions[i].level = 0;
        }
        regions[i].page_layout = layout;
    }
    regions[i].index = i;
    regions[i].level = 0;

    if(width >= height){
        /* 横屏 */
        /*	 iYres/5
		 *	  ----------------------------------		  
		 *    home          
		 *    go back
		 *    pre_pic
		 *    next_pic
		 *    unfold/fold zoom_in zoom_out left_rotate right_rotate reset_pic    
		 *	  ----------------------------------
		 */
        unit_distance = height / 5;
        y_cursor = 0;
        x_cursor = 0;
        /* "home" */
        regions[0].x_pos    = x_cursor;
        regions[0].y_pos    = y_cursor;
        regions[0].height   = unit_distance;
        regions[0].width    = unit_distance;
        
        /* "goback" */
        regions[1].x_pos    = x_cursor ;
        regions[1].y_pos    = y_cursor + unit_distance;
        regions[1].height   = unit_distance;
        regions[1].width    = unit_distance;
        
        /* "pre_pic" */
        regions[2].x_pos    = x_cursor;
        regions[2].y_pos    = y_cursor + unit_distance * 2;
        regions[2].height   = unit_distance;
        regions[2].width    = unit_distance;
        
        /* "next_pic" */
        regions[3].x_pos    = x_cursor;
        regions[3].y_pos    = y_cursor + unit_distance * 3;
        regions[3].height   = unit_distance;
        regions[3].width    = unit_distance;
        
        /* "unfold" */
        regions[4].x_pos    = x_cursor;
        regions[4].y_pos    = y_cursor + unit_distance * 4;
        regions[4].height   = unit_distance;
        regions[4].width    = unit_distance;
        
        /* 图片显示区域 */
        regions[REGION_MAIN_PIC].x_pos    = x_cursor + unit_distance;
        regions[REGION_MAIN_PIC].y_pos    = 0;
        regions[REGION_MAIN_PIC].height   = height ;
        regions[REGION_MAIN_PIC].width    = width - unit_distance;

        unfold_region_interval = 10;
        unfold_region_width = unit_distance * 2 / 3;
        x_cursor = unit_distance + unfold_region_interval;
        y_cursor = unit_distance * 4 + (unit_distance - unfold_region_width) / 2;

        /* zoom_in,zoom_out,left_rotate,right_rotate,pic_reset */
        for(i = 6 ; i < 11 ; i++){
            regions[i].x_pos    = x_cursor;
            regions[i].y_pos    = y_cursor;
            x_cursor += unfold_region_interval + unfold_region_width;
            regions[i].height   = unfold_region_width;
            regions[i].width    = unfold_region_width ;
        }
    }else{
        /* 竖屏 */
        /*	 iXres/4
		 *	  --------------------------------------
		 *	        
		 *                                     pic_reset
		 *                                     right_rotate
		 *                                     left_rotate
		 *                                     zoom_out
		 *                                     zoom_in
		 *    home  goback  pre_pic  next_pic  unfold/fold
		 *	  -------------------------------------
		 */
       /* 思路是一样的,时间不够,就不写了 */
    }

    /* 设置相应标志位 */
    page->already_layout = 1;

    return 0;
}

/* 很遗憾，这个函数要求图标长宽必须相同，否则会出现什么我也不确定 */
/* 注意,此函数执行完后会将图标缩放至和是大小,但bpp是与原图像一致的,在合并时需要注意 */
static int prepare_menu_icon_data(struct page_struct *page)
{
    int i,ret;
    struct pixel_data pixel_data;
    struct page_region *regions = page->page_layout.regions;
    struct picfmt_parser *png_parser = get_parser_by_name("png");
    const char file_path[] = DEFAULT_ICON_FILE_PATH;
    char file_full_path[100];
    
    memset(&pixel_data,0,sizeof(pixel_data));
    for(i = 0 ; i < MENU_ICON_NUMS ; i++){
        char *file_name;
        int file_name_malloc = 0;

        /* 为了预防文件名过长导致出错 */
        if((strlen(file_path) + strlen(menu_icon_files[i]) + 1) > 99){
            file_name = malloc(strlen(file_path) + strlen(menu_icon_files[i]) + 2);
            if(!file_name){
                DP_ERR("%s:malloc failed!\n");
                return -ENOMEM;
            }
            sprintf(file_name,"%s/%s",file_path,menu_icon_files[i]);
            file_name_malloc = 1;
        }else{
            sprintf(file_full_path,"%s/%s",file_path,menu_icon_files[i]);
        }
        
        if(file_name_malloc){
            ret = png_parser->get_pixel_data_in_rows(file_name,&pixel_data);
        }else{
            ret = png_parser->get_pixel_data_in_rows(file_full_path,&pixel_data);
        } 
        if(ret){
            if(ret == -2){
                //to-do 此种错误是可修复的
            }
            DP_ERR("%s:png get_pixel_data_in_rows failed!\n",__func__);
            return -ENOMEM;
        } 
        /* 计算图标要缩放到的大小，并为其分配内存 */
        /* 特别注意,第5个区域是显示图片的主体区域 */
        if(i < REGION_MAIN_PIC){
            menu_icon_datas[i].width = regions[0].width;
            menu_icon_datas[i].height = regions[0].height;
        }else if(10 == i){
            menu_icon_datas[i].width = regions[0].width;
            menu_icon_datas[i].height = regions[0].height;
        }else if(i >= REGION_MAIN_PIC){
            menu_icon_datas[i].width = regions[6].width;
            menu_icon_datas[i].height = regions[6].height;
        }
        
        menu_icon_datas[i].bpp = pixel_data.bpp;
        menu_icon_datas[i].line_bytes = menu_icon_datas[i].width * menu_icon_datas[i].bpp / 8;
        menu_icon_datas[i].total_bytes = menu_icon_datas[i].line_bytes * menu_icon_datas[i].height;
        menu_icon_datas[i].buf = malloc(menu_icon_datas[i].total_bytes);
        
        if(!menu_icon_datas[i].buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        } 
        /* 数据是整块缓存的，要去除相应标志 */
        memset(menu_icon_datas[i].buf,0xff,menu_icon_datas[i].total_bytes);
        ret = pic_zoom_with_same_bpp(&menu_icon_datas[i],&pixel_data);
        if(ret){
            DP_ERR("%s:pic_zoom_with_same_bpp failed!\n",__func__);
            return -1;
        }

        if(file_name_malloc){
            free(file_name);
        }
    }
    /* 释放由png解析函数分配的内存 */
    if(pixel_data.in_rows){
        for(i = 0 ; i < pixel_data.height ; i++){
            free(pixel_data.rows_buf[i]);
        }
        free(pixel_data.rows_buf);
    }

    page->icon_prepared = 1;

    return 0;
}

/* 在此函数中将会计算好页面的布局情况,并且准备好图标数据 */
static int view_pic_page_init(void)
{
    int ret;
    struct display_struct *default_display = get_default_display();
    struct page_layout *page_layout = &view_pic_page.page_layout;
    struct page_region *regions = page_layout->regions;
    int width = default_display->xres;
    int height = default_display->yres;

    page_layout->width  = width;
    page_layout->height = height;
    view_pic_page.page_mem.bpp     = default_display->bpp;
    view_pic_page.page_mem.width   = width;
    view_pic_page.page_mem.height  = height;
    view_pic_page.page_mem.line_bytes  = view_pic_page.page_mem.width * view_pic_page.page_mem.bpp / 8;
    view_pic_page.page_mem.total_bytes = view_pic_page.page_mem.line_bytes * view_pic_page.page_mem.height;

    /* 计算布局 */
    ret = view_pic_page_calc_layout(&view_pic_page);
    if(ret){
        DP_ERR("%s:view_pic_page_calc_layout failed!\n",__func__);
        return ret;
    }

    // ret = prepare_menu_icon_data(&view_pic_page);
    if(ret){
        DP_ERR("%s:prepare_menu_icon_data failed!\n",__func__);
        return -1;
    }

    return 0;
}

/* 对应的销毁函数，可能用不上，但按理说应该是需要的 */
static void destroy_menu_icon_data(void)
{
    int i;
    for(i = 0 ; i < MENU_ICON_NUMS ; i++){
        free(menu_icon_datas[i].buf);
    }
}

/* 退出函数 */
static void view_pic_page_exit(void)
{
    struct page_region *regions = view_pic_page.page_layout.regions;
    int i;
    /* 删除映射 */
    if(view_pic_page.region_mapped){
        for(i = 0 ; i < view_pic_page.page_layout.region_num ; i++){
            if(regions[i].pixel_data->in_rows){
                free(regions[i].pixel_data->rows_buf);
            }
            free(regions[i].pixel_data);
        }
    }

    /* 删除布局 */
    if(view_pic_page.already_layout){
        free(view_pic_page.page_layout.regions);
    }

    /* 释放占用的内存 */
    if(view_pic_page.allocated){
        free(view_pic_page.page_mem.buf);
    }

    destroy_menu_icon_data();
}

/* 将图片重置为能在屏幕上显示的大小，注意：这个函数只能对保留有原有数据的缓存使用,save_orig参数是说明是否要保留原始数据 */
static int reset_pic_cache_size(struct pic_cache *pic_cache,bool save_orig)
{
    float scale;
    int ret;
    unsigned int display_width,display_height;
    unsigned int zoomed_width,zoomed_height;
    unsigned int orig_width,orig_height;
    struct pixel_data *pixel_data;

    if(!pic_cache->has_orig_data){
        return 0;              //没有原始图像数据，直接退出
    }

    orig_width = pic_cache->orig_width;
    orig_height = pic_cache->orig_height;
    pixel_data = &pic_cache->data;

    /* 获取显示区域的长宽 */
    scale = (float)orig_height / orig_width;
    display_width = view_pic_page.page_layout.regions[REGION_MAIN_PIC].width;
    display_height = view_pic_page.page_layout.regions[REGION_MAIN_PIC].height;

    /* 确定缩放后的图片大小 */
    if(display_width >= orig_width && display_height >= orig_height){
        zoomed_width = orig_width;
        zoomed_height = orig_height;
    }else if(display_width < orig_width && display_height < orig_height){
        /* 先将宽度缩至允许的最大值 */
        zoomed_width = display_width;
        zoomed_height = scale * zoomed_width;
        if(zoomed_height > display_height){
            /* 还要继续缩小 */
            zoomed_height = display_height;
            zoomed_width = zoomed_height / scale;
        }
    }else if(display_width < orig_width){
        zoomed_width = display_width;
        zoomed_height = zoomed_width * scale;
    }else if(display_height < orig_height){
        zoomed_height = display_height;
        zoomed_width = zoomed_height / scale;
    }

    /* 记录图像在虚拟显示空间中的左上角座标 */
    pic_cache->virtual_x = (display_width - zoomed_width) / 2;
    pic_cache->virtual_y = (display_height - zoomed_height) / 2;

    /* 检查当前缓存的数据大小是否符合要求，要是符合在这里就可以退出了，否则从原始数据中复制一份过去 */
    if(!(!pic_cache->angle && pic_cache->has_data && zoomed_width == pixel_data->width && \
    zoomed_height == pixel_data->height)){
        if(pic_cache->has_data){
            /* 释放原有数据 */
            free(pixel_data->buf);
            pic_cache->has_data = 0;
        }
        memset(pixel_data,0,sizeof(struct pixel_data));
        pixel_data->width = zoomed_width;
        pixel_data->height = zoomed_height;
        pic_cache->angle = 0;
        ret = pic_zoom_with_same_bpp(pixel_data,pic_cache->orig_data);
        if(ret < 0){
            DP_ERR("%s:pic_zoom_with_same_bpp failed!\n",__func__);
            return ret;
        }
        pic_cache->has_data = 1;
    }
    /* 如果需要释放原有数据 */
    if(!save_orig){
        pixel_data = (struct pixel_data*)pic_cache->orig_data;
        free(pixel_data->buf);
        free(pixel_data);
        pic_cache->has_orig_data = 0;
        pic_cache->orig_data = NULL;
    }
    return 0;
}

/* 给定当前目录信息数组的一个索引，将对应的文件读入并将大小设为初始值,save_orig表示是否保存原有数据 */
static int get_pic_cache_data(int pic_index,struct pic_cache **pic_cache,bool save_orig)
{
    int ret;
    char *pic_file;
    struct pixel_data *pixel_data;
    struct pic_cache *temp_cache;

    /* 构造文件名 */DP_INFO("%d\n",__LINE__);
    pic_file = malloc(strlen(cur_dir) + 2 + strlen(cur_dir_pic_contents[pic_index]->name));
    if(!pic_file){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    sprintf(pic_file,"%s/%s",cur_dir,cur_dir_pic_contents[pic_index]->name);DP_INFO("pic_file:%s\n",pic_file);
    
    /* 分配一个struct pixel_data */
    pixel_data = malloc(sizeof(struct pixel_data));
    if(!pixel_data){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    memset(pixel_data,0,sizeof(struct pixel_data));
    ret = get_pic_pixel_data(pic_file,cur_dir_pic_contents[pic_index]->file_type,pixel_data);DP_INFO("%d\n",__LINE__);
    free(pic_file);
    
    /* 分配一个struct pic_cache */
    temp_cache = malloc(sizeof(struct pic_cache));
    if(!temp_cache){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    memset(temp_cache,0,sizeof(struct pic_cache));

    /* 先将原始数据临时关联到pic_caches上 */
    temp_cache->orig_data = pixel_data;
    temp_cache->has_orig_data = 1;

    /* 填充pic_cache;  保存图像的原始长宽 */
    temp_cache->orig_width = pixel_data->width;
    temp_cache->orig_height = pixel_data->height;
    
    /* 将读入的图片重置为能在屏幕上显示的大小,如果图片能完全被显示,那么不做改变,否则会缩放至合适大小 */
    ret = reset_pic_cache_size(temp_cache,save_orig);
    if(ret < 0){
        DP_ERR("%s:reset_pic_size failed!\n",__func__);
        return -1;
    }
    temp_cache->has_data = 1;
    *pic_cache = temp_cache;        //返回数据

    return 0;
}

/* 读入三张图并缓存 */
static int generate_pic_cache(void)
{
    int i,ret;
    int pre_index,next_index;

    /* 做一些最基本的检查 */
    if(!cur_dir || !cur_dir_pic_contents){
        return -1;
    }

    /* 如果缓存已存在,先将其释放 */
    if(pic_caches_generated){
        for(i = 0 ; i < 3 ; i++){
            if(pic_caches[i]){
                if(pic_caches[i]->has_data){
                    free(pic_caches[i]->data.buf);
                }
                if(pic_caches[i]->has_orig_data){
                    struct pixel_data *temp = (struct pixel_data*)pic_caches[i]->orig_data;
                    free(temp->buf);
                    free(temp);
                }
                free(pic_caches[i]);
            }   
        }
        pic_caches_generated = 0;
    }

    /* 生成前一张,当前,下一张这三张图片的缓存*/
    if(1 == cur_dir_pic_nums){
        /* 只有一张图，只生成当前图片的缓存就可以了 */
        ret = get_pic_cache_data(cur_pic_index,&pic_caches[1],1);
        if(ret < 0){
            DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
            return -1;
        }
    }else if(2 == cur_dir_pic_nums){
        ret = get_pic_cache_data(cur_pic_index,&pic_caches[1],1);
        if(ret < 0){
            DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
            return -1;
        }
        if(0 == cur_pic_index){  
            ret = get_pic_cache_data(cur_pic_index + 1,&pic_caches[2],0);
            if(ret < 0){
                DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
                return -1;
            }
        }else if(1 == cur_pic_index){
            ret = get_pic_cache_data(cur_pic_index - 1,&pic_caches[0],0);
            if(ret < 0){
                DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
                return -1;
            }
        }
    }else{
        /* 求出前一张图和后一张图在目录数组中的索引，索引求出来了，后面的调函数就可以了 */
        if((pre_index = cur_pic_index - 1) < 0){
            pre_index = cur_dir_pic_nums - 1;
        }
        if((next_index = cur_pic_index + 1) == cur_dir_pic_nums){
            next_index = 0;
        }DP_INFO("pre_index:%d,cur_pic_index:%d,next_index:%d,\n",pre_index,cur_pic_index,next_index);
        ret = get_pic_cache_data(cur_pic_index,&pic_caches[1],1);DP_INFO("%d\n",__LINE__);
        if(ret < 0){
            DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
            return -1;
        }
        ret = get_pic_cache_data(pre_index,&pic_caches[0],0);DP_INFO("%d\n",__LINE__);
        if(ret < 0){
            DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
            return -1;
        }
        ret = get_pic_cache_data(next_index,&pic_caches[2],0);DP_INFO("%d\n",__LINE__);
        if(ret < 0){
            DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
            return -1;
        }
    }DP_INFO("%d\n",__LINE__);
    /* 设置相应标志位 */
    pic_caches_generated = 1;

    return 0;
}

static int fill_menu_icon_area(struct page_struct *page)
{
    int i,ret;
    struct page_region *regions = page->page_layout.regions;
    for(i = 0 ; i < REGION_MAIN_PIC ; i++){
        /* 没有数据直接跳过 */
        if(!menu_icon_datas[i].buf){
            continue;
        }
        ret = merge_pixel_data(regions[i].pixel_data,&menu_icon_datas[i]);
        if(ret < 0){
            DP_ERR("%s:merge_pixel_data failed!\n",__func__);
            return ret;
        }
    }
    return 0;
}

static int fill_unfolded_menu_icon_area(struct page_struct *page)
{
    int i,ret;
    struct page_region *regions = page->page_layout.regions;
    for(i = REGION_MENU_ZOOM_IN ; i <= REGION_MENU_PIC_RESET ; i++){
        /* 没有数据直接跳过 */
        if(!menu_icon_datas[i - 1].buf){
            continue;
        }
        ret = merge_pixel_data(regions[i].pixel_data,&menu_icon_datas[i - 1]);
        if(ret < 0){
            DP_ERR("%s:merge_pixel_data failed!\n",__func__);
            return ret;
        }
    }
 
    return 0;
}

/* 其实这个函数要比想象中的复杂，它负责将图像缓存中的数据更新到页面对应的内存中，
 * 但关键的是，因为图像可以拖动，它要负责处理图片在显示区域中的位置问题，这是很麻烦的， */
static int fill_main_pic_area(struct page_struct *page)
{
    int i,j,ret;
    struct display_struct *default_display = get_default_display();
    struct page_region *main_pic_region = &page->page_layout.regions[REGION_MAIN_PIC];
    struct pic_cache *cur_pic = *cur_pic_data;
    struct pixel_data *src_data,*dst_data;
    unsigned char *dst_line_buf,*src_line_buf;
    unsigned char src_red,src_green,src_blue,alpha;
    unsigned char dst_red,dst_green,dst_blue;
    unsigned char red,green,blue;
    unsigned char src_bpp;
    unsigned short color;
    short region_width,region_height;       //显示区域的整体宽高
    short x_disp,y_disp;                    //显示区域的左上角座标
    short disp_width,disp_height;           //实际显示图片区域的宽高
    short x_pic,y_pic;                      //图像中被显示的部分在图像内的左上角座标
    short x_vpic,y_vpic;                    //图像在虚拟空间中的左上角座标
    short pic_width,pic_height;             //图像的长宽

    dst_data = main_pic_region->pixel_data;
    src_data = &cur_pic->data;
    src_bpp = src_data->bpp;
    DP_INFO("enter:%s\n",__func__);DP_INFO("cur_pic->has_data:%d\n",cur_pic->has_data);
    DP_INFO("dst_data->bpp:%d,src_data->bpp:%d\n",dst_data->bpp,src_data->bpp);
    //如果一些条件不满足，快退出把，不要浪费时间了
    if(!page->region_mapped || !cur_pic->has_data || dst_data->bpp != 16 || \
    (src_data->bpp != 16 && src_data->bpp != 24 && src_data->bpp != 32)){     
        return -1;
    }
    DP_INFO("cur_pic->has_data:%d\n",cur_pic->has_data);
    /* 先清理该区域 */
    clear_pixel_data(dst_data,BACKGROUND_COLOR);
DP_INFO("%d\n",__LINE__);
    /* 先解出显示相关的几个座标 */
    x_vpic = cur_pic->virtual_x;
    y_vpic = cur_pic->virtual_y;
    region_width  = main_pic_region->width;
    region_height = main_pic_region->height;
    pic_width = src_data->width;
    pic_height = src_data->height;
    /* 先解决x方向 */
    if(x_vpic < 0){DP_INFO("%d\n",__LINE__);
        if((x_vpic + pic_width - 1) < 0){
            return 0;           //图像已经超出显示区域了
        }else if((x_vpic + pic_width - 1) < region_width){
            x_disp = 0;
            disp_width = x_vpic + pic_width;
            x_pic = (-x_vpic);
        }else{
            x_disp = 0;
            disp_width = region_width;
            x_pic = (-x_vpic);
        }
    }else if(x_vpic < region_width){DP_INFO("%d\n",__LINE__);
        if((x_vpic + pic_width - 1) < region_width){
            x_pic = 0;
            disp_width = pic_width;
            x_disp = x_vpic;
        }else{
            x_pic = 0;
            disp_width = region_width - x_vpic;
            x_disp = x_vpic;
        }
    }else{
        return 0;           //图像已经超出显示区域了
    }
    /* 再解决y方向 */
    if(y_vpic < 0){DP_INFO("%d\n",__LINE__);
        if((y_vpic + pic_height - 1) < 0){DP_INFO("%d\n",__LINE__);
            return 0;           //图像已经超出显示区域了
        }else if((y_vpic + pic_height) < region_height){
            y_disp = 0;
            disp_height = y_vpic + pic_height - 1;
            y_pic = (-y_vpic);
        }else{
            y_disp = 0;
            disp_height = region_height;
            y_pic = (-y_vpic);
        }
    }else if(y_vpic < region_height){DP_INFO("%d\n",__LINE__);
        if((y_vpic + pic_height - 1) < region_height){
            y_pic = 0;
            disp_height = pic_height;
            y_disp = y_vpic;
        }else{
            y_pic = 0;
            disp_height = region_height - y_vpic;
            y_disp = y_vpic;
        }
    }else{
        return 0;           //图像已经超出显示区域了
    }
    DP_INFO("x_pic:%d,y_pic:%d,disp_width:%d,disp_height:%d\n",x_pic,y_pic,disp_width,disp_height);
    /* 位置总算确定好了，开始描绘像素，希望能成功，阿弥陀佛 */
    /* 目标区域默认为16bpp，源数据区域可为16、24、32bpp，否则报错 */
    for(i = 0 ; i < disp_height ; i++){
        if(src_data->in_rows){
            src_line_buf = src_data->rows_buf[y_pic + i] + x_pic * src_bpp / 8;
        }else{
            src_line_buf = src_data->buf + src_data->line_bytes * (y_pic + i) + x_pic * src_bpp / 8;
        }
        if(dst_data->in_rows){
            dst_line_buf = dst_data->rows_buf[y_disp + i] + x_disp * 2;
        }else{
            dst_line_buf = dst_data->buf + dst_data->line_bytes * (y_disp + i) + x_disp * 2;
        }
        //根据不同bpp作不同处理
        switch(src_bpp){
            case 16:
                memcpy(dst_line_buf,src_line_buf,disp_width * 2);
                break;
            case 24:
                for(j = 0 ; j < disp_width ; j++){
                    /* 取出各颜色分量 */
                    red   = src_line_buf[j * 3] >> 3;
                    green = src_line_buf[j * 3 + 1] >> 2;
                    blue  = src_line_buf[j * 3 + 2] >> 3;
                    color = (red << 11) | (green << 5) | blue;

                    *(unsigned short *)dst_line_buf = color;
                    // *(unsigned short *)dst_line_buf = 0xff;
                    dst_line_buf += 2;
                }
                break;
            case 32:
                for(j = 0 ; j < disp_width ; j++){
                    /* 取出各颜色分量 */
                    alpha     = src_line_buf[j * 4];
                    src_red   = src_line_buf[j * 4 + 1] >> 3;
                    src_green = src_line_buf[j * 4 + 2] >> 2;
                    src_blue  = src_line_buf[j * 4 + 3] >> 3;

                    color = *(unsigned short *)dst_line_buf;
                    dst_red   = (color >> 11) & 0x1f;
                    dst_green = (color >> 5) & 0x3f;
                    dst_blue  = color & 0x1f;

                    /* 根据透明度合并颜色,公式:显示颜色 = 源像素颜色 X alpha / 255 + 背景颜色 X (255 - alpha) / 255 */
                    red   = (src_red * alpha) / 255 + (dst_red * (255 - alpha)) / 255;
                    green = (src_green * alpha) / 255 + (dst_green * (255 - alpha)) / 255;
                    blue  = (src_blue * alpha) / 255 + (dst_blue * (255 - alpha)) / 255;
                    color = (red << 11) | (green << 5) | blue;

                    *(unsigned short *)dst_line_buf = color;
                    // *(unsigned short *)dst_line_buf = 0xff;
                    dst_line_buf += 2;
                }
                break;
            default:
                return -1;
                break;
        }
    }

    /* 如果当前菜单是展开状态，将展开菜单填充上去 */
    if(menu_unfolded){
        ret = fill_unfolded_menu_icon_area(page);
        if(ret < 0){
            DP_ERR("%s:fill_unfolded_menu_icon_area failed!\n",__func__);
            return ret;
        }
    }
    
    return 0;
}

 /* 将主体显示区域中的变动更新到显存中 */
static void flush_main_pic_area(struct page_struct *page)
{
    struct display_struct *default_display = get_default_display();
    struct page_region *main_pic_region = &page->page_layout.regions[REGION_MAIN_PIC];

    flush_page_region(main_pic_region,default_display);
}

/* 一切准备好后,用于填充各区域 */
static int view_pic_page_fill_layout(struct page_struct *page)
{
    int ret;

    /* 如果想加个整体的背景，应该最先加进去 */
    //...
    DP_ERR("enter:%s\n",__func__);
    
    /* 检查菜单图标数据 */
    if(!page->icon_prepared){
        ret = prepare_menu_icon_data(page);
        if(ret){
            DP_ERR("%s:prepare_menu_icon_data failed!\n",__func__);
            return -1;
        }
        page->icon_prepared = 1;
    }
    /* 填充菜单图标数据 */
    ret = fill_menu_icon_area(page);DP_INFO("%d\n",__LINE__);
    if(ret){
        DP_ERR("%s:fill_menu_icon_area failed!\n",__func__);
        return -1;
    }
    /* 填充主体图像显示区域 */
    ret = fill_main_pic_area(page);DP_INFO("%d\n",__LINE__);
    if(ret){
        DP_ERR("%s:fill_main_pic_area failed!\n",__func__);
        return -1;
    }
    return 0;
}

/* 点击返回菜单时的回调函数 */
void goback_menu_cb_func(void)
{
    char *tmp;
    char *buf_end;
    /* 如果当前是根目录,什么也不做 */
    if(!strcmp(cur_dir,"/")){
        return;
    }

    /*  */
}

/* 点击"上一张"菜单时的回调函数 */
static int prepic_menu_cb_func(void)
{   
    struct pic_cache *temp;
    int ret;
    int pre_index;
    char *pic_file;DP_INFO("enter:%s\n",__func__);
    /* 如果当前目录只有一张图，则什么也不做 */
    if(1 == cur_dir_pic_nums){
        return 0;
    }else if(2 == cur_dir_pic_nums){
        if(0 == cur_pic_index){
            cur_pic_index = 1;
            temp = pic_caches[1];
            pic_caches[1] = pic_caches[2];
            pic_caches[0] = temp;
        }else if(1 == cur_pic_index){
            cur_pic_index = 0;
            temp = pic_caches[1];
            pic_caches[1] = pic_caches[0];
            pic_caches[2] = temp;
        }
        ret = reset_pic_cache_size(temp,0);
        /* 释放缓存中的原始数据 */
        if(ret < 0){
            DP_ERR("%s:reset_pic_size failed!\n",__func__);
            return -1;
        }
        fill_main_pic_area(&view_pic_page);
    }else if(3 == cur_dir_pic_nums){
        if((cur_pic_index -= 1) < 0){
            cur_pic_index = 2;
        }
        temp = pic_caches[1];
        pic_caches[1] = pic_caches[0];
        pic_caches[0] = pic_caches[2];
        pic_caches[2] = temp;
        ret = reset_pic_cache_size(temp,0);
        if(ret < 0){
            DP_ERR("%s:reset_pic_size failed!\n",__func__);
            return -1;
        }
        fill_main_pic_area(&view_pic_page);
    }else{
        if((cur_pic_index -= 1) < 0){
            cur_pic_index = cur_dir_pic_nums - 1;
        }
        if((pre_index = cur_pic_index - 1) < 0){
            pre_index = cur_dir_pic_nums - 1;
        }
        temp = pic_caches[1];
        ret = reset_pic_cache_size(temp,0);
        if(ret < 0){
            DP_ERR("%s:reset_pic_size failed!\n",__func__);
            return -1;
        }
        /* 先显示图片，释放和缓存操作放在后面 */
        pic_caches[1] = pic_caches[0];
        fill_main_pic_area(&view_pic_page);
        flush_main_pic_area(&view_pic_page);
        
        /* 释放下一张的缓存 */
        if(pic_caches[2] && pic_caches[2]->has_data){
            free(pic_caches[2]->data.buf);
            free(pic_caches[2]);
            pic_caches[2] = NULL;
        }
        
        pic_caches[2] = temp;
        
        /* 读入上一张的缓存 */
        ret = get_pic_cache_data(pre_index,&pic_caches[0],0);
        if(ret < 0){
            DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
            return -ENOMEM;
        }
    }
    return 0;
}

/* 点击"下一张"菜单时的回调函数 */
static int nextpic_menu_cb_func(void)
{
    struct pic_cache *temp;
    int ret;
    int next_index;
    char *pic_file;
    /* 如果当前目录只有一张图，则什么也不做 */
    if(1 == cur_dir_pic_nums){
        return 0;
    }else if(2 == cur_dir_pic_nums){
        if(0 == cur_pic_index){
            cur_pic_index = 1;
            temp = pic_caches[1];
            pic_caches[1] = pic_caches[2];
            pic_caches[0] = temp;
        }else if(1 == cur_pic_index){
            cur_pic_index = 0;
            temp = pic_caches[1];
            pic_caches[1] = pic_caches[0];
            pic_caches[2] = temp;
        }
        ret = reset_pic_cache_size(temp,0);
        if(ret < 0){
            DP_ERR("%s:reset_pic_size failed!\n",__func__);
            return -1;
        }
        fill_main_pic_area(&view_pic_page);
        flush_main_pic_area(&view_pic_page);
    }else if(3 == cur_dir_pic_nums){
        if((cur_pic_index += 1) == cur_dir_pic_nums){
            cur_pic_index = 0;
        }
        temp = pic_caches[1];
        pic_caches[1] = pic_caches[2];
        pic_caches[2] = pic_caches[0];
        pic_caches[1] = temp;
        ret = reset_pic_cache_size(temp,0);
        if(ret < 0){
            DP_ERR("%s:reset_pic_size failed!\n",__func__);
            return -1;
        }
        fill_main_pic_area(&view_pic_page);
        flush_main_pic_area(&view_pic_page);
    }else{
        if((cur_pic_index += 1) == cur_dir_pic_nums){
            cur_pic_index = 0;
        }
        if((next_index = cur_pic_index + 1) == cur_dir_pic_nums){
            next_index = 0;
        }
        temp = pic_caches[1];
        ret = reset_pic_cache_size(temp,0);
        if(ret < 0){
            DP_ERR("%s:reset_pic_size failed!\n",__func__);
            return -1;
        }
        /* 先显示图片，释放和缓存操作放在后面 */
        pic_caches[1] = pic_caches[2];
        fill_main_pic_area(&view_pic_page);
        flush_main_pic_area(&view_pic_page);
        DP_INFO("%d\n",__LINE__);
        /* 释放上一张的缓存 */
        if(pic_caches[0] && pic_caches[0]->has_data){
            free(pic_caches[0]->data.buf);
            free(pic_caches[0]);
            pic_caches[0] = NULL;
        }
        
        pic_caches[0] = temp;
        
        /* 读入下一张的缓存 */
        ret = get_pic_cache_data(next_index,&pic_caches[2],0);
        if(ret < 0){
            DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
            return -ENOMEM;
        }
    }
    return 0;
}

/* 点击"更多"菜单时的回调函数 */
static int unfold_menu_cb_func(void)
{
    int ret;
    struct page_region *regions = view_pic_page.page_layout.regions;
    struct display_struct *default_dsiplay = get_default_display();

    /* 如果当前是折叠状态 */
    if(!menu_unfolded){
        /* 将各个图标的数据复制到页面缓冲对应的位置中 */
        ret = fill_unfolded_menu_icon_area(&view_pic_page);
        if(ret < 0){
            DP_ERR("%s:fill_unfolded_menu_icon_area failed!\n",__func__);
            return ret;
        }
        clear_pixel_data(regions[REGION_MENU_UNFOLD].pixel_data,0xffff);
        merge_pixel_data(regions[REGION_MENU_UNFOLD].pixel_data,&menu_icon_datas[MENU_ICON_FOLD]);
        /* 设置相应标志位 */
        menu_unfolded = 1;
        /* 将改动冲洗到缓存中 */
        flush_page_region(&regions[REGION_MENU_UNFOLD],default_dsiplay);
        flush_page_region(&regions[REGION_MENU_ZOOM_IN],default_dsiplay);
        flush_page_region(&regions[REGION_MENU_ZOOM_OUT],default_dsiplay);
        flush_page_region(&regions[REGION_MENU_LEFT_ROTATE],default_dsiplay);
        flush_page_region(&regions[REGION_MENU_RIGHT_ROTATE],default_dsiplay);
        flush_page_region(&regions[REGION_MENU_PIC_RESET],default_dsiplay);
    }else{
        menu_unfolded = 0;
        /* 如果已经是展开状态，重新显示主体图片，覆盖图标，并设置相应标志 */
        clear_pixel_data(regions[REGION_MENU_UNFOLD].pixel_data,0xffff);
        merge_pixel_data(regions[REGION_MENU_UNFOLD].pixel_data,&menu_icon_datas[MENU_ICON_UNFOLD]);
        flush_page_region(&regions[REGION_MENU_UNFOLD],default_dsiplay);
        fill_main_pic_area(&view_pic_page);     //原有图像已被破坏，需要重新填充
        flush_main_pic_area(&view_pic_page);
        
    }
    return 0;
}

/* 点击"放大"菜单时的回调函数 */
static int zoomin_menu_cb_func(void)
{
    int ret;
    float scale;
    char *cur_file;
    unsigned int zoomed_width,zoomed_height;
    struct pic_cache *cur_pic = *cur_pic_data;
    struct pixel_data *pixel_data = &cur_pic->data;;

    /* 先判断能不能放大，在这里，图像再怎么放大，也不会超过原图大小 */
    if((cur_pic->angle == 0 || cur_pic->angle == 180) && cur_pic->data.width >= cur_pic->orig_width){
        return 0;
    }else if((cur_pic->angle == 90 || cur_pic->angle == 270) && cur_pic->data.width >= cur_pic->orig_height){
        return 0;   //这是图片被旋转后再缩放的情况
    }

    /* 先看看当前图片的原图数据是否存在，如果不存在则进行读取 */
    if(!cur_pic->has_orig_data){
        cur_file = malloc(strlen(cur_dir) + 2 + strlen(cur_dir_pic_contents[cur_pic_index]->name));
        if(!cur_file){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        sprintf(cur_file,"%s/%s",cur_dir,cur_dir_pic_contents[cur_pic_index]->name);
        cur_pic->orig_data = malloc(sizeof(struct pixel_data));
        if(!cur_pic->orig_data){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        memset(cur_pic->orig_data,0,sizeof(struct pixel_data));
        ret = get_pic_pixel_data(cur_file,cur_dir_pic_contents[cur_pic_index]->file_type,cur_pic->orig_data);
        if(ret < 0){
            DP_ERR("%s:get_pic_pixel_data failed!\n",__func__);
            free(cur_pic->orig_data);
            return ret;
        }
        cur_pic->has_orig_data = 1;
        free(cur_file);
    }
    
    /* 计算缩放后的图像长宽及起始位置，缩放是以图片中心为原点的，而位置调整只需重新设置pic_cache中的虚拟座标，会有函数完成其他部分的 */
    scale = (float)(pixel_data->height) / pixel_data->width;
    zoomed_width = pixel_data->width / ZOOM_RATE;
    zoomed_height = zoomed_width * scale;
    if(zoomed_width >= cur_pic->orig_width || zoomed_width >= cur_pic->orig_height){
        return 0;
    }
    
    cur_pic->virtual_x -= ((zoomed_width - pixel_data->width) / 2);
    cur_pic->virtual_y -= ((zoomed_height - pixel_data->height) / 2);
    
    /* 释放原有数据 */
    if(cur_pic->has_data){
        free(pixel_data->buf);
        cur_pic->has_data = 0;
    }
    memset(pixel_data,0,sizeof(struct pixel_data));
    pixel_data->width = zoomed_width;
    pixel_data->height = zoomed_height;
    
    /* 开始缩放 */
    ret = pic_zoom_with_same_bpp_and_rotate(cur_pic->orig_data,pixel_data,cur_pic->angle);
    if(ret < 0){
        DP_ERR("%s:pic_zoom_with_same_bpp_and_rotate failed!\n",__func__);
        free(pixel_data);
        return ret;
    }

    cur_pic->has_data = 1;

    /* 将更改刷入缓存 */
    fill_main_pic_area(&view_pic_page);
    flush_main_pic_area(&view_pic_page);
    return 0;
}

/* 点击"缩小"菜单时的回调函数 */
static int zoomout_menu_cb_func(void)
{
    static unsigned int min_width = 40;     //设置一个缩放的最小值
    static unsigned int min_height = 40;
    int ret;
    float scale;
    char *cur_file;
    unsigned int zoomed_width,zoomed_height;
    struct pic_cache *cur_pic = *cur_pic_data;
    struct pixel_data *pixel_data = &cur_pic->data;

    /* 小于指定值就不要再缩小了 */
    if(cur_pic->data.width <= min_width || cur_pic->data.height <= min_height){
        return 0;
    }

    /* 计算缩放后的图像长宽以及起始位置 */
    scale = (float)(pixel_data->height) / pixel_data->width;
    zoomed_width = pixel_data->width * ZOOM_RATE;
    zoomed_height = zoomed_width * scale;
    cur_pic->virtual_x += ((pixel_data->width - zoomed_width) / 2);
    cur_pic->virtual_y += ((pixel_data->height - zoomed_height) / 2);
    
    /* 只要触发了缩放操作，不管缩小还是放大，如果不存在原始数据，都重新读入原始数据进行操作 */
    /* 先看看当前图片的原图数据是否存在，如果不存在则进行读取 */
    if(!cur_pic->has_orig_data){
        cur_file = malloc(strlen(cur_dir) + 2 + strlen(cur_dir_pic_contents[cur_pic_index]->name));
        if(!cur_file){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        sprintf(cur_file,"%s/%s",cur_dir,cur_dir_pic_contents[cur_pic_index]->name);
        cur_pic->orig_data = malloc(sizeof(struct pixel_data));
        if(!cur_pic->orig_data){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        memset(cur_pic->orig_data,0,sizeof(struct pixel_data));
        ret = get_pic_pixel_data(cur_file,cur_dir_pic_contents[cur_pic_index]->file_type,cur_pic->orig_data);
        if(ret < 0){
            DP_ERR("%s:get_pic_pixel_data failed!\n",__func__);
            free(cur_pic->orig_data);
            return ret;
        }
        cur_pic->has_orig_data = 1;
        free(cur_file);
    }
    
    /* 释放原有数据 */
    if(cur_pic->has_data){
        free(pixel_data->buf);
        cur_pic->has_data = 0;
    }
    memset(pixel_data,0,sizeof(struct pixel_data));
    pixel_data->width = zoomed_width;
    pixel_data->height = zoomed_height;

    /* 开始缩放 */
    // ret = pic_zoom_with_same_bpp(cur_pic->orig_data,pixel_data);
    ret = pic_zoom_with_same_bpp_and_rotate(cur_pic->orig_data,pixel_data,cur_pic->angle);
    if(ret < 0){
        DP_ERR("%s:pic_zoom_with_same_bpp_and_rotate failed!\n",__func__);
        return -ENOMEM;
    }
    cur_pic->has_data = 1;
    
    /* 将更改刷入缓存 */
    fill_main_pic_area(&view_pic_page);
    flush_main_pic_area(&view_pic_page);

    return 0;
}

/* 点击"左转"菜单时的回调函数 */
static int leftrotate_menu_cb_func(void)
{
    struct pic_cache *cur_pic = *cur_pic_data;
    struct pixel_data *pixel_data = &cur_pic->data;
    struct pixel_data temp_data;
    unsigned char *dst_line_buf,*src_col_buf;
    unsigned short orig_width,orig_height;
    unsigned short rotated_width,rotated_height;
    unsigned short x_center,y_center;       //计算辅助点
    unsigned short i,j,k;
    unsigned short bytes_per_pixel;
    unsigned short src_line_bytes;

    /* 不管怎样，还是检查一下吧，内存问题真的怕 */
    if(!cur_pic->has_data || !pixel_data->buf){
        return -1;
    }
    /* 这里就简单点考虑了，如果数据不是整块存储直接退出 */
    if(pixel_data->in_rows){
        return -1;
    }

    orig_width = pixel_data->width;
    orig_height = pixel_data->height;
    rotated_width = orig_height;
    rotated_height = orig_width;
    bytes_per_pixel = pixel_data->bpp / 8;
    src_line_bytes = pixel_data->line_bytes;

    /* 分配一块临时内存 */
    temp_data.width = rotated_width;
    temp_data.height = rotated_height;
    temp_data.bpp = pixel_data->bpp;
    temp_data.line_bytes = rotated_width * bytes_per_pixel;
    temp_data.total_bytes = temp_data.line_bytes * temp_data.height;

    temp_data.buf = malloc(temp_data.total_bytes);
    if(!temp_data.buf){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    
    /* 旋转像素 */
    for(i = 0 ; i < rotated_height ; i++){
        dst_line_buf = temp_data.buf + i * temp_data.line_bytes;
        src_col_buf = pixel_data->buf + (orig_width - i - 1) * bytes_per_pixel; 
        for(j = 0 ; j < rotated_width ; j++){
            switch(bytes_per_pixel){
                case 1:
                    *dst_line_buf = *(src_col_buf + j * src_line_bytes);
                    dst_line_buf++;
                    break;
                case 2:
                    *(unsigned short *)dst_line_buf = *(unsigned short *)(src_col_buf + j * src_line_bytes);
                    dst_line_buf += 2;
                    break;
                case 4:
                    *(unsigned int *)dst_line_buf = *(unsigned int *)(src_col_buf + j * src_line_bytes);
                    dst_line_buf += 4;
                    break;
                default:
                    for(k = 0 ; k < bytes_per_pixel ; k++){
                        *(dst_line_buf + k) = *(src_col_buf + j * src_line_bytes + k);
                    }
                    dst_line_buf += bytes_per_pixel;
                    break;
            }
        }
    }

    /* 更新数据 */
    free(pixel_data->buf);
    *pixel_data = temp_data;
    if((cur_pic->angle -= 90) < 0){
        cur_pic->angle = 270;
    }
    /* 修改虚拟起点，旋转是相对于图像中心进行的 */
    x_center = cur_pic->virtual_x + orig_width / 2;
    y_center = cur_pic->virtual_y + orig_height / 2;
    cur_pic->virtual_x = x_center - rotated_width / 2;
    cur_pic->virtual_y = y_center -rotated_height / 2; 

    /* 将更改后的图像缓存刷入页面缓存，然后再刷入屏幕显存 */
    fill_main_pic_area(&view_pic_page);
    flush_main_pic_area(&view_pic_page);

    return 0;
}

/* 点击"右转"菜单时的回调函数 */
static int rightrotate_menu_cb_func(void)
{
    struct pic_cache *cur_pic = *cur_pic_data;
    struct pixel_data *pixel_data = &cur_pic->data;
    struct pixel_data temp_data;
    unsigned char *dst_line_buf,*src_col_buf,*last_row_buf;
    unsigned short orig_width,orig_height;
    unsigned short rotated_width,rotated_height;
    unsigned short x_center,y_center;       //计算辅助点
    unsigned short i,j,k;
    unsigned short bytes_per_pixel;
    unsigned short src_line_bytes;

    /* 不管怎样，还是检查一下吧，内存问题真的怕 */
    if(!cur_pic->has_data || !pixel_data->buf){
        return -1;
    }
    /* 这里就简单点考虑了，如果数据不是整块存储直接退出 */
    if(pixel_data->in_rows){
        return -1;
    }

    orig_width = pixel_data->width;
    orig_height = pixel_data->height;
    rotated_width = orig_height;
    rotated_height = orig_width;
    bytes_per_pixel = pixel_data->bpp / 8;
    src_line_bytes = pixel_data->line_bytes;

    /* 分配一块临时内存 */
    temp_data.width = rotated_width;
    temp_data.height = rotated_height;
    temp_data.bpp = pixel_data->bpp;
    temp_data.line_bytes = rotated_width * bytes_per_pixel;
    temp_data.total_bytes = temp_data.line_bytes * temp_data.height;

    temp_data.buf = malloc(temp_data.total_bytes);
    if(!temp_data.buf){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    
    /* 旋转像素 */
    last_row_buf = pixel_data->buf + src_line_bytes * (orig_height - 1);     //指向最后一行行缓存起始地址
    for(i = 0 ; i < rotated_height ; i++){
        dst_line_buf = temp_data.buf + i * temp_data.line_bytes;
        src_col_buf = last_row_buf + i * bytes_per_pixel; 
        for(j = 0 ; j < rotated_width ; j++){
            switch(bytes_per_pixel){
                case 1:
                    *dst_line_buf = *(src_col_buf - j * src_line_bytes);
                    dst_line_buf++;
                    break;
                case 2:
                    *(unsigned short *)dst_line_buf = *(unsigned short *)(src_col_buf - j * src_line_bytes);
                    dst_line_buf += 2;
                    break;
                case 4:
                    *(unsigned int *)dst_line_buf = *(unsigned int *)(src_col_buf - j * src_line_bytes);
                    dst_line_buf += 4;
                    break;
                default:
                    for(k = 0 ; k < bytes_per_pixel ; k++){
                        *(dst_line_buf + k) = *(src_col_buf - j * src_line_bytes + k);
                    }
                    dst_line_buf += bytes_per_pixel;
                    break;
            }
        }
    }

    /* 更新数据 */
    free(pixel_data->buf);
    *pixel_data = temp_data;
    if((cur_pic->angle -= 90) < 0){
        cur_pic->angle = 270;
    }
    /* 修改虚拟起点，旋转是相对于图像中心进行的 */
    x_center = cur_pic->virtual_x + orig_width / 2;
    y_center = cur_pic->virtual_y + orig_height / 2;
    cur_pic->virtual_x = x_center - rotated_width / 2;
    cur_pic->virtual_y = y_center -rotated_height / 2; 

    /* 将更改后的图像缓存刷入页面缓存，然后再刷入屏幕显存 */
    fill_main_pic_area(&view_pic_page);
    flush_main_pic_area(&view_pic_page);

    return 0;
}

/* 点击"重置"菜单时的回调函数 */
static int picreset_menu_cb_func(void)
{
    int ret;
    struct pic_cache *cur_pic = *cur_pic_data;
    ret = reset_pic_cache_size(cur_pic,1);
    if(ret < 0){
        DP_ERR("%s:reset_pic_cache_size failed!\n",__func__);
        return ret;
    }

    /* 将更改后的图像缓存刷入页面缓存，然后再刷入屏幕显存 */
    fill_main_pic_area(&view_pic_page);
    flush_main_pic_area(&view_pic_page);
    return 0;
}

/* 入口函数,主要功能：分配内存；解析要显示的数据；while循环检测输入*/
static int view_pic_page_run(struct page_param *pre_page_param)
{
    short ret;
    short pre_region_index = -1;
    short region_index;
    short slot_id = -1;
    short pressure = 0;
    short x_pre_drag = 0;      /* 手指在屏幕上滑动时，前一个点的座标 */ 
    short y_pre_drag = 0;
    short x_offset,y_offset;
    unsigned short distance;
    struct display_struct *default_display;
    struct page_region *regions;
    DP_ERR("enter:%s\n",__func__);
    /* 为该页面分配一块内存 */
    if(!view_pic_page.allocated){
        view_pic_page.page_mem.buf = malloc(view_pic_page.page_mem.total_bytes);
        if(!view_pic_page.page_mem.buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -1;
        }
        view_pic_page.allocated = 1;
    }
    /* 注意，页面布局在注册该页面时，在初始化函数中已经计算好了 */

    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!view_pic_page.region_mapped){
        ret = remap_regions_to_page_mem(&view_pic_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return -1;
        }
    }
    
    /* 将页面清为白色 */
    clear_pixel_data(&view_pic_page.page_mem,0xffff);
    
    /* 获取目录信息 */
    ret = get_pic_dir_contents(pre_page_param->private_data,&cur_dir_pic_contents,&cur_dir_pic_nums,&cur_pic_index,&cur_dir);
    if(ret < 0){
        DP_ERR("%s:get_pic_dir_contents failed!\n",__func__);
        return -1;
    }DP_INFO("cur_dir_pic_nums:%d\n",cur_dir_pic_nums);
    /* 准备主体的图像数据缓存，该函数使用全局变量 */
    ret = generate_pic_cache(); DP_INFO("%d\n",__LINE__);
    if(ret < 0){
        DP_ERR("%s:generate_pic_cache failed!\n",__func__);
        return -1;
    }
    
    /* 填充各区域 */
    ret = view_pic_page_fill_layout(&view_pic_page);DP_INFO("%d\n",__LINE__);
    if(ret){
        DP_ERR("%s:view_pic_page_fill_layout failed!\n",__func__);
        return -1;
    }   

    default_display = get_default_display();
    default_display->flush_buf(default_display,view_pic_page.page_mem.buf,view_pic_page.page_mem.total_bytes);
    
    regions = view_pic_page.page_layout.regions;

    /* 检测输入事件的循环 */
    while(1){
        struct my_input_event event;
        region_index = get_input_event_for_page(&view_pic_page,&event);
        DP_ERR("region_index:%d!\n",region_index);
        /* 触摸屏支持多触点事件,这里暂时只响应单个触点 */
        /* 后面希望能实现两指缩放功能 */
        if(-1 == slot_id){
            slot_id = event.slot_id;
        }else if(slot_id != event.slot_id){
            continue;
        }
        //只处理特定区域内的事件
        if(region_index < 0 || (!menu_unfolded && region_index >= REGION_MENU_ZOOM_IN && region_index <= REGION_MENU_PIC_RESET )){
            if(!event.presssure && (-1 != pre_region_index)){
                invert_region(regions[pre_region_index].pixel_data);
                flush_page_region(&regions[pre_region_index],default_display);
                pre_region_index = -1;
                pressure = 0;
                slot_id = -1;
            }
            continue;           
        }
        if(event.presssure){                //按下
            if(0 == pressure){     //还未曾有按钮按下   
                pre_region_index = region_index;DP_ERR("pre_region_index:%d!\n",pre_region_index);
                pressure = 1;
                 /* 反转按下区域的颜色 */
                if(region_index != 5){
                    invert_region(regions[region_index].pixel_data);
                    flush_page_region(&regions[region_index],default_display);  
                }  
            }
            if(REGION_MAIN_PIC == region_index){    //该需区域可响应连续的按下时间，也就是滑动
                if(!x_pre_drag && !y_pre_drag){     //首次按下
                    x_pre_drag = event.x_pos;
                    y_pre_drag = event.y_pos;
                }else{                              //后续的拖动点
                    /* 距离按平方算，节省时间 */
                    distance = (event.y_pos - y_pre_drag) * (event.y_pos - y_pre_drag) + \
                     (event.x_pos - x_pre_drag) * (event.x_pos - x_pre_drag);
                    if(distance > min_drag_distance){   /* 如果距离大于最小值，则进行响应 */
                        x_offset = event.x_pos - x_pre_drag;
                        y_offset = event.y_pos - y_pre_drag;
                        (*cur_pic_data)->virtual_x += x_offset;
                        (*cur_pic_data)->virtual_y += y_offset;
                        fill_main_pic_area(&view_pic_page);
                        flush_main_pic_area(&view_pic_page);
                        x_pre_drag = event.x_pos;
                        y_pre_drag = event.y_pos;
                    }
                }
            }
        }else{                  //松开
            if(!pressure) continue;
            if(region_index == REGION_MAIN_PIC){
                x_pre_drag = 0;
                y_pre_drag = 0;
            }
            /* 按下和松开的是同一个区域，这是一次有效的点击 */
            if(region_index == pre_region_index){
                if(region_index != 5){
                    invert_region(regions[region_index].pixel_data);
                    flush_page_region(&regions[region_index],default_display);  
                }
                switch (region_index){
                    case REGION_MENU_HOME:               /* home */
                        return 0;
                        break;
                    case REGION_MENU_GOBACK:             /* goback */
                        return 0;
                        break;
                    case REGION_MENU_PRE_PIC:            /* pre_pic */
                        prepic_menu_cb_func();
                        break;
                    case REGION_MENU_NEXT_PIC:           /* next_pic */
                        nextpic_menu_cb_func();
                        break;
                    case REGION_MENU_UNFOLD:             /* unfolded */
                        unfold_menu_cb_func();
                        break;
                    case REGION_MENU_ZOOM_IN:
                        zoomin_menu_cb_func();
                        break;
                    case REGION_MENU_ZOOM_OUT: 
                        zoomout_menu_cb_func();           
                        break;
                    case REGION_MENU_LEFT_ROTATE:
                        leftrotate_menu_cb_func(); 
                        break;
                    case REGION_MENU_RIGHT_ROTATE:
                        rightrotate_menu_cb_func(); 
                        break;
                    case REGION_MENU_PIC_RESET:
                        picreset_menu_cb_func(); 
                        break;
                    case REGION_MAIN_PIC:           //主体图像显示区域
                        
                        break;
                    default:            /* 文件区域 */
                        
                        break;
                }
            }else{
                invert_region(regions[pre_region_index].pixel_data);
                flush_page_region(&regions[pre_region_index],default_display);
            }
            pressure = 0;
            slot_id = -1;
            pre_region_index = -1;
        }
    }   
    return 0;
}

static struct page_struct view_pic_page = {
    .name = "view_pic_page",
    .init = view_pic_page_init,
    .exit = view_pic_page_exit,
    .run  = view_pic_page_run,
    .allocated = 0,
};

int view_pic_init(void)
{
    return register_page_struct(&view_pic_page);
}