/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 * SPDX-FileCopyrightText: 2003-2009 Blender Authors
 * SPDX-FileCopyrightText: 2005-2006 Peter Schlaile <peter [at] schlaile [dot] de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 */

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "MEM_guardedalloc.h"

#include "DNA_scene_types.h"
#include "DNA_sequence_types.h"

#include "BLI_blenlib.h"
#include "BLI_vector_set.hh"

#include "BLT_translation.hh"

#include "BKE_animsys.h"
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "BKE_scene.hh"

#include "SEQ_channels.hh"
#include "SEQ_edit.hh"
#include "SEQ_iterator.hh"
#include "SEQ_relations.hh"
#include "SEQ_render.hh"
#include "SEQ_select.hh"
#include "SEQ_sequencer.hh"
#include "SEQ_time.hh"
#include "SEQ_utils.hh"

#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"

#include "MOV_read.hh"

#include "multiview.hh"
#include "proxy.hh"
#include "sequencer.hh"
#include "utils.hh"

struct SeqUniqueInfo {
  Strip *seq;
  char name_src[SEQ_NAME_MAXSTR];
  char name_dest[SEQ_NAME_MAXSTR];
  int count;
  int match;
};

static void seqbase_unique_name(ListBase *seqbasep, SeqUniqueInfo *sui)
{
  LISTBASE_FOREACH (Strip *, seq, seqbasep) {
    if ((sui->seq != seq) && STREQ(sui->name_dest, seq->name + 2)) {
      /* SEQ_NAME_MAXSTR -4 for the number, -1 for \0, - 2 for r_prefix */
      SNPRINTF(
          sui->name_dest, "%.*s.%03d", SEQ_NAME_MAXSTR - 4 - 1 - 2, sui->name_src, sui->count++);
      sui->match = 1; /* be sure to re-scan */
    }
  }
}

static bool seqbase_unique_name_recursive_fn(Strip *seq, void *arg_pt)
{
  if (seq->seqbase.first) {
    seqbase_unique_name(&seq->seqbase, (SeqUniqueInfo *)arg_pt);
  }
  return true;
}

void SEQ_sequence_base_unique_name_recursive(Scene *scene, ListBase *seqbasep, Strip *seq)
{
  SeqUniqueInfo sui;
  char *dot;
  sui.seq = seq;
  STRNCPY(sui.name_src, seq->name + 2);
  STRNCPY(sui.name_dest, seq->name + 2);

  sui.count = 1;
  sui.match = 1; /* assume the worst to start the loop */

  /* Strip off the suffix */
  if ((dot = strrchr(sui.name_src, '.'))) {
    *dot = '\0';
    dot++;

    if (*dot) {
      sui.count = atoi(dot) + 1;
    }
  }

  while (sui.match) {
    sui.match = 0;
    seqbase_unique_name(seqbasep, &sui);
    SEQ_for_each_callback(seqbasep, seqbase_unique_name_recursive_fn, &sui);
  }

  SEQ_edit_sequence_name_set(scene, seq, sui.name_dest);
}

static const char *give_seqname_by_type(int type)
{
  switch (type) {
    case SEQ_TYPE_META:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Meta");
    case SEQ_TYPE_IMAGE:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Image");
    case SEQ_TYPE_SCENE:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Scene");
    case SEQ_TYPE_MOVIE:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Movie");
    case SEQ_TYPE_MOVIECLIP:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Clip");
    case SEQ_TYPE_MASK:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Mask");
    case SEQ_TYPE_SOUND_RAM:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Audio");
    case SEQ_TYPE_CROSS:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Cross");
    case SEQ_TYPE_GAMCROSS:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Gamma Cross");
    case SEQ_TYPE_ADD:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Add");
    case SEQ_TYPE_SUB:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Subtract");
    case SEQ_TYPE_MUL:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Multiply");
    case SEQ_TYPE_ALPHAOVER:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Alpha Over");
    case SEQ_TYPE_ALPHAUNDER:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Alpha Under");
    case SEQ_TYPE_OVERDROP:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Over Drop");
    case SEQ_TYPE_COLORMIX:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Color Mix");
    case SEQ_TYPE_WIPE:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Wipe");
    case SEQ_TYPE_GLOW:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Glow");
    case SEQ_TYPE_TRANSFORM:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Transform");
    case SEQ_TYPE_COLOR:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Color");
    case SEQ_TYPE_MULTICAM:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Multicam");
    case SEQ_TYPE_ADJUSTMENT:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Adjustment");
    case SEQ_TYPE_SPEED:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Speed");
    case SEQ_TYPE_GAUSSIAN_BLUR:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Gaussian Blur");
    case SEQ_TYPE_TEXT:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_SEQUENCE, "Text");
    default:
      return nullptr;
  }
}

const char *SEQ_sequence_give_name(const Strip *seq)
{
  const char *name = give_seqname_by_type(seq->type);

  if (!name) {
    if (!(seq->type & SEQ_TYPE_EFFECT)) {
      return seq->data->dirpath;
    }

    return DATA_("Effect");
  }
  return name;
}

ListBase *SEQ_get_seqbase_from_sequence(Strip *seq, ListBase **r_channels, int *r_offset)
{
  ListBase *seqbase = nullptr;

  switch (seq->type) {
    case SEQ_TYPE_META: {
      seqbase = &seq->seqbase;
      *r_channels = &seq->channels;
      *r_offset = SEQ_time_start_frame_get(seq);
      break;
    }
    case SEQ_TYPE_SCENE: {
      if (seq->flag & SEQ_SCENE_STRIPS && seq->scene) {
        Editing *ed = SEQ_editing_get(seq->scene);
        if (ed) {
          seqbase = &ed->seqbase;
          *r_channels = &ed->channels;
          *r_offset = seq->scene->r.sfra;
        }
      }
      break;
    }
  }

  return seqbase;
}

static void open_anim_filepath(Strip *seq, StripAnim *sanim, const char *filepath, bool openfile)
{
  if (openfile) {
    sanim->anim = openanim(filepath,
                           IB_rect | ((seq->flag & SEQ_FILTERY) ? IB_animdeinterlace : 0),
                           seq->streamindex,
                           seq->data->colorspace_settings.name);
  }
  else {
    sanim->anim = openanim_noload(filepath,
                                  IB_rect | ((seq->flag & SEQ_FILTERY) ? IB_animdeinterlace : 0),
                                  seq->streamindex,
                                  seq->data->colorspace_settings.name);
  }
}

static bool use_proxy(Editing *ed, Strip *seq)
{
  StripProxy *proxy = seq->data->proxy;
  return proxy && ((proxy->storage & SEQ_STORAGE_PROXY_CUSTOM_DIR) != 0 ||
                   (ed->proxy_storage == SEQ_EDIT_PROXY_DIR_STORAGE));
}

static void proxy_dir_get(Editing *ed, Strip *seq, size_t str_len, char *r_proxy_dirpath)
{
  if (use_proxy(ed, seq)) {
    if (ed->proxy_storage == SEQ_EDIT_PROXY_DIR_STORAGE) {
      if (ed->proxy_dir[0] == 0) {
        BLI_strncpy(r_proxy_dirpath, "//BL_proxy", str_len);
      }
      else {
        BLI_strncpy(r_proxy_dirpath, ed->proxy_dir, str_len);
      }
    }
    else {
      BLI_strncpy(r_proxy_dirpath, seq->data->proxy->dirpath, str_len);
    }
    BLI_path_abs(r_proxy_dirpath, BKE_main_blendfile_path_from_global());
  }
}

static void index_dir_set(Editing *ed, Strip *seq, StripAnim *sanim)
{
  if (sanim->anim == nullptr || !use_proxy(ed, seq)) {
    return;
  }

  char proxy_dirpath[FILE_MAX];
  proxy_dir_get(ed, seq, sizeof(proxy_dirpath), proxy_dirpath);
  seq_proxy_index_dir_set(sanim->anim, proxy_dirpath);
}

static bool open_anim_file_multiview(Scene *scene, Strip *seq, const char *filepath)
{
  char prefix[FILE_MAX];
  const char *ext = nullptr;
  BKE_scene_multiview_view_prefix_get(scene, filepath, prefix, &ext);

  if (seq->views_format != R_IMF_VIEWS_INDIVIDUAL || prefix[0] == '\0') {
    return false;
  }

  Editing *ed = scene->ed;
  bool is_multiview_loaded = false;
  int totfiles = seq_num_files(scene, seq->views_format, true);

  for (int i = 0; i < totfiles; i++) {
    const char *suffix = BKE_scene_multiview_view_id_suffix_get(&scene->r, i);
    char filepath_view[FILE_MAX];
    SNPRINTF(filepath_view, "%s%s%s", prefix, suffix, ext);

    StripAnim *sanim = static_cast<StripAnim *>(MEM_mallocN(sizeof(StripAnim), "Strip Anim"));
    /* Multiview files must be loaded, otherwise it is not possible to detect failure. */
    open_anim_filepath(seq, sanim, filepath_view, true);

    if (sanim->anim == nullptr) {
      SEQ_relations_sequence_free_anim(seq);
      return false; /* Multiview render failed. */
    }

    index_dir_set(ed, seq, sanim);
    BLI_addtail(&seq->anims, sanim);
    MOV_set_multiview_suffix(sanim->anim, suffix);
    is_multiview_loaded = true;
  }

  return is_multiview_loaded;
}

void seq_open_anim_file(Scene *scene, Strip *seq, bool openfile)
{
  if ((seq->anims.first != nullptr) && (((StripAnim *)seq->anims.first)->anim != nullptr) &&
      !openfile)
  {
    return;
  }

  /* Reset all the previously created anims. */
  SEQ_relations_sequence_free_anim(seq);

  Editing *ed = scene->ed;
  char filepath[FILE_MAX];
  BLI_path_join(filepath, sizeof(filepath), seq->data->dirpath, seq->data->stripdata->filename);
  BLI_path_abs(filepath, ID_BLEND_PATH_FROM_GLOBAL(&scene->id));

  bool is_multiview = (seq->flag & SEQ_USE_VIEWS) != 0 && (scene->r.scemode & R_MULTIVIEW) != 0;
  bool multiview_is_loaded = false;

  if (is_multiview) {
    multiview_is_loaded = open_anim_file_multiview(scene, seq, filepath);
  }

  if (!is_multiview || !multiview_is_loaded) {
    StripAnim *sanim = static_cast<StripAnim *>(MEM_mallocN(sizeof(StripAnim), "Strip Anim"));
    BLI_addtail(&seq->anims, sanim);
    open_anim_filepath(seq, sanim, filepath, openfile);
    index_dir_set(ed, seq, sanim);
  }
}

const Strip *SEQ_get_topmost_sequence(const Scene *scene, int frame)
{
  Editing *ed = scene->ed;

  if (!ed) {
    return nullptr;
  }

  ListBase *channels = SEQ_channels_displayed_get(ed);
  const Strip *best_seq = nullptr;
  int best_machine = -1;

  LISTBASE_FOREACH (const Strip *, seq, ed->seqbasep) {
    if (SEQ_render_is_muted(channels, seq) || !SEQ_time_strip_intersects_frame(scene, seq, frame))
    {
      continue;
    }
    /* Only use strips that generate an image, not ones that combine
     * other strips or apply some effect. */
    if (ELEM(seq->type,
             SEQ_TYPE_IMAGE,
             SEQ_TYPE_META,
             SEQ_TYPE_SCENE,
             SEQ_TYPE_MOVIE,
             SEQ_TYPE_COLOR,
             SEQ_TYPE_TEXT))
    {
      if (seq->machine > best_machine) {
        best_seq = seq;
        best_machine = seq->machine;
      }
    }
  }
  return best_seq;
}

ListBase *SEQ_get_seqbase_by_seq(const Scene *scene, Strip *seq)
{
  Editing *ed = SEQ_editing_get(scene);
  ListBase *main_seqbase = &ed->seqbase;
  Strip *seq_meta = seq_sequence_lookup_meta_by_seq(scene, seq);

  if (seq_meta != nullptr) {
    return &seq_meta->seqbase;
  }
  if (BLI_findindex(main_seqbase, seq) != -1) {
    return main_seqbase;
  }
  return nullptr;
}

Strip *SEQ_sequence_from_strip_elem(ListBase *seqbase, StripElem *se)
{
  Strip *iseq;

  for (iseq = static_cast<Strip *>(seqbase->first); iseq; iseq = iseq->next) {
    Strip *seq_found;
    if ((iseq->data && iseq->data->stripdata) &&
        ARRAY_HAS_ITEM(se, iseq->data->stripdata, iseq->len))
    {
      break;
    }
    if ((seq_found = SEQ_sequence_from_strip_elem(&iseq->seqbase, se))) {
      iseq = seq_found;
      break;
    }
  }

  return iseq;
}

Strip *SEQ_get_sequence_by_name(ListBase *seqbase, const char *name, bool recursive)
{
  LISTBASE_FOREACH (Strip *, iseq, seqbase) {
    if (STREQ(name, iseq->name + 2)) {
      return iseq;
    }
    if (recursive && !BLI_listbase_is_empty(&iseq->seqbase)) {
      Strip *rseq = SEQ_get_sequence_by_name(&iseq->seqbase, name, true);
      if (rseq != nullptr) {
        return rseq;
      }
    }
  }

  return nullptr;
}

Mask *SEQ_active_mask_get(Scene *scene)
{
  Strip *seq_act = SEQ_select_active_get(scene);

  if (seq_act && seq_act->type == SEQ_TYPE_MASK) {
    return seq_act->mask;
  }

  return nullptr;
}

void SEQ_alpha_mode_from_file_extension(Strip *seq)
{
  if (seq->data && seq->data->stripdata) {
    const char *filename = seq->data->stripdata->filename;
    seq->alpha_mode = BKE_image_alpha_mode_from_extension_ex(filename);
  }
}

bool SEQ_sequence_has_valid_data(const Strip *seq)
{
  switch (seq->type) {
    case SEQ_TYPE_MASK:
      return (seq->mask != nullptr);
    case SEQ_TYPE_MOVIECLIP:
      return (seq->clip != nullptr);
    case SEQ_TYPE_SCENE:
      return (seq->scene != nullptr);
    case SEQ_TYPE_SOUND_RAM:
      return (seq->sound != nullptr);
  }

  return true;
}

bool sequencer_seq_generates_image(Strip *seq)
{
  switch (seq->type) {
    case SEQ_TYPE_IMAGE:
    case SEQ_TYPE_SCENE:
    case SEQ_TYPE_MOVIE:
    case SEQ_TYPE_MOVIECLIP:
    case SEQ_TYPE_MASK:
    case SEQ_TYPE_COLOR:
    case SEQ_TYPE_TEXT:
      return true;
  }
  return false;
}

void SEQ_set_scale_to_fit(const Strip *seq,
                          const int image_width,
                          const int image_height,
                          const int preview_width,
                          const int preview_height,
                          const eSeqImageFitMethod fit_method)
{
  StripTransform *transform = seq->data->transform;

  switch (fit_method) {
    case SEQ_SCALE_TO_FIT:
      transform->scale_x = transform->scale_y = std::min(
          float(preview_width) / float(image_width), float(preview_height) / float(image_height));

      break;
    case SEQ_SCALE_TO_FILL:

      transform->scale_x = transform->scale_y = std::max(
          float(preview_width) / float(image_width), float(preview_height) / float(image_height));
      break;
    case SEQ_STRETCH_TO_FILL:
      transform->scale_x = float(preview_width) / float(image_width);
      transform->scale_y = float(preview_height) / float(image_height);
      break;
    case SEQ_USE_ORIGINAL_SIZE:
      transform->scale_x = 1.0f;
      transform->scale_y = 1.0f;
      break;
  }
}

void SEQ_ensure_unique_name(Strip *seq, Scene *scene)
{
  char name[SEQ_NAME_MAXSTR];

  STRNCPY_UTF8(name, seq->name + 2);
  SEQ_sequence_base_unique_name_recursive(scene, &scene->ed->seqbase, seq);
  BKE_animdata_fix_paths_rename(&scene->id,
                                scene->adt,
                                nullptr,
                                "sequence_editor.sequences_all",
                                name,
                                seq->name + 2,
                                0,
                                0,
                                false);

  if (seq->type == SEQ_TYPE_META) {
    LISTBASE_FOREACH (Strip *, seq_child, &seq->seqbase) {
      SEQ_ensure_unique_name(seq_child, scene);
    }
  }
}
