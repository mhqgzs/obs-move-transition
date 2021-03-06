#include "move-transition.h"
#include <obs-module.h>
#include <stdio.h>
#include <util/dstr.h>

struct move_source_info {
	obs_source_t *source;
	char *source_name;
	char *filter_name;
	obs_sceneitem_t *scene_item;
	obs_hotkey_id move_start_hotkey;

	long long easing;
	long long easing_function;
	float curve;

	struct vec2 pos_from;
	struct vec2 pos_to;
	float rot_from;
	float rot_to;
	struct vec2 scale_from;
	struct vec2 scale_to;
	struct vec2 bounds_from;
	struct vec2 bounds_to;
	struct obs_sceneitem_crop crop_from;
	struct obs_sceneitem_crop crop_to;
	uint64_t duration;
	uint64_t start_delay;
	uint64_t end_delay;
	bool moving;
	float running_duration;
	uint32_t canvas_width;
	uint32_t canvas_height;
	uint32_t start_trigger;
	bool enabled;
	char *next_move_name;
	bool relative;
	DARRAY(obs_source_t *) filters_done;

	long long next_move_on;
	long long change_visibility;
	bool visibility_toggled;
	bool reverse;

	long long change_order;
	int order_position;
};

bool find_sceneitem(obs_scene_t *scene, obs_sceneitem_t *scene_item, void *data)
{
	UNUSED_PARAMETER(scene);
	struct move_source_info *move_source = data;
	const char *name =
		obs_source_get_name(obs_sceneitem_get_source(scene_item));
	if (name && strcmp(name, move_source->source_name) == 0) {
		obs_sceneitem_addref(scene_item);
		move_source->scene_item = scene_item;
		return false;
	}
	return true;
}

void calc_relative_to(struct move_source_info *move_source, float f)
{
	obs_data_t *settings = obs_source_get_settings(move_source->source);
	move_source->rot_to = move_source->rot_from +
			      (float)obs_data_get_double(settings, S_ROT) * f;
	struct vec2 vec2;
	obs_data_get_vec2(settings, S_POS, &vec2);
	move_source->pos_to.x = move_source->pos_from.x + vec2.x * f;
	move_source->pos_to.y = move_source->pos_from.y + vec2.y * f;
	obs_data_get_vec2(settings, S_SCALE, &vec2);
	move_source->scale_to.x = move_source->scale_from.x + vec2.x * f;
	move_source->scale_to.y = move_source->scale_from.y + vec2.y * f;
	obs_data_get_vec2(settings, S_BOUNDS, &vec2);
	move_source->bounds_to.x = move_source->bounds_from.x + vec2.x * f;
	move_source->bounds_to.y = move_source->bounds_from.y + vec2.y * f;
	move_source->crop_to.left =
		move_source->crop_from.left +
		(int)obs_data_get_int(settings, S_CROP_LEFT) * (int)f;
	move_source->crop_to.top =
		move_source->crop_from.top +
		(int)obs_data_get_int(settings, S_CROP_TOP) * (int)f;
	move_source->crop_to.right =
		move_source->crop_from.right +
		(int)obs_data_get_int(settings, S_CROP_RIGHT) * (int)f;
	move_source->crop_to.bottom =
		move_source->crop_from.bottom +
		(int)obs_data_get_int(settings, S_CROP_BOTTOM) * (int)f;
	obs_data_release(settings);
}

void move_source_start(struct move_source_info *move_source)
{
	if (!move_source->scene_item && move_source->source_name &&
	    strlen(move_source->source_name)) {
		obs_source_t *parent =
			obs_filter_get_parent(move_source->source);
		if (parent) {
			obs_scene_t *scene = obs_scene_from_source(parent);
			if (scene)
				obs_scene_enum_items(scene, find_sceneitem,
						     move_source);
		}
	}
	if (!move_source->scene_item)
		return;
	if ((move_source->change_order & CHANGE_ORDER_START) != 0) {
		if ((move_source->change_order & CHANGE_ORDER_RELATIVE) != 0 &&
		    move_source->order_position) {
			if (move_source->order_position > 0) {
				for (int i = 0; i < move_source->order_position;
				     i++) {
					obs_sceneitem_set_order(
						move_source->scene_item,
						OBS_ORDER_MOVE_UP);
				}
			} else if (move_source->order_position < 0) {
				for (int i = 0; i > move_source->order_position;
				     i--) {
					obs_sceneitem_set_order(
						move_source->scene_item,
						OBS_ORDER_MOVE_DOWN);
				}
			}
		} else if ((move_source->change_order &
			    CHANGE_ORDER_ABSOLUTE) != 0) {
			obs_sceneitem_set_order_position(
				move_source->scene_item,
				move_source->order_position);
		}
	}
	if ((move_source->change_visibility == CHANGE_VISIBILITY_SHOW ||
	     move_source->change_visibility == CHANGE_VISIBILITY_TOGGLE) &&
	    !obs_sceneitem_visible(move_source->scene_item)) {
		obs_sceneitem_set_visible(move_source->scene_item, true);
		move_source->visibility_toggled = true;
	} else {
		move_source->visibility_toggled = false;
	}
	move_source->running_duration = 0.0f;
	if (!move_source->reverse) {
		move_source->rot_from =
			obs_sceneitem_get_rot(move_source->scene_item);
		obs_sceneitem_get_pos(move_source->scene_item,
				      &move_source->pos_from);
		obs_sceneitem_get_scale(move_source->scene_item,
					&move_source->scale_from);
		obs_sceneitem_get_bounds(move_source->scene_item,
					 &move_source->bounds_from);
		obs_sceneitem_get_crop(move_source->scene_item,
				       &move_source->crop_from);
		obs_source_t *scene_source = obs_scene_get_source(
			obs_sceneitem_get_scene(move_source->scene_item));
		move_source->canvas_width = obs_source_get_width(scene_source);
		move_source->canvas_height =
			obs_source_get_height(scene_source);

		if (move_source->relative) {
			calc_relative_to(move_source, 1.0f);
		}
	} else if (move_source->relative) {
		calc_relative_to(move_source, -1.0f);
	}
	if (move_source->rot_from != move_source->rot_to ||
	    move_source->pos_from.x != move_source->pos_to.x ||
	    move_source->pos_from.y != move_source->pos_to.y ||
	    move_source->scale_from.x != move_source->scale_to.x ||
	    move_source->scale_from.y != move_source->scale_to.y ||
	    move_source->bounds_from.x != move_source->bounds_to.x ||
	    move_source->bounds_from.y != move_source->bounds_to.y ||
	    move_source->crop_from.left != move_source->crop_to.left ||
	    move_source->crop_from.top != move_source->crop_to.top ||
	    move_source->crop_from.right != move_source->crop_to.right ||
	    move_source->crop_from.bottom != move_source->crop_to.bottom ||
	    (move_source->change_visibility == CHANGE_VISIBILITY_HIDE &&
	     obs_sceneitem_visible(move_source->scene_item)) ||
	    (move_source->change_visibility == CHANGE_VISIBILITY_TOGGLE &&
	     !move_source->visibility_toggled) ||
	    move_source->visibility_toggled) {
		move_source->moving = true;
	} else if (move_source->start_trigger == START_TRIGGER_ENABLE_DISABLE) {
		obs_source_set_enabled(move_source->source, false);
	}
}

bool move_source_start_button(obs_properties_t *props, obs_property_t *property,
			      void *data)
{
	struct move_source_info *move_source = data;
	move_source_start(move_source);
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	return false;
}

void move_value_start(struct move_value_info *move_value);

void move_source_start_hotkey(void *data, obs_hotkey_id id,
			      obs_hotkey_t *hotkey, bool pressed)
{
	if (!pressed)
		return;
	struct move_source_info *move_source = data;
	if (move_source->next_move_on != NEXT_MOVE_ON_HOTKEY ||
	    !move_source->next_move_name ||
	    !strlen(move_source->next_move_name)) {
		move_source_start(move_source);
		return;
	}
	if (!move_source->filters_done.num) {
		move_source_start(move_source);
		da_push_back(move_source->filters_done, &move_source->source);
		return;
	}

	char *next_move_name = move_source->next_move_name;
	obs_source_t *filter = move_source->source;
	obs_source_t *parent = obs_filter_get_parent(filter);
	obs_source_t *source =
		obs_sceneitem_get_source(move_source->scene_item);
	long long next_move_on = move_source->next_move_on;
	size_t i = 0;
	while (i < move_source->filters_done.num) {
		if (!next_move_name || !strlen(next_move_name)) {
			move_source_start(move_source);
			move_source->filters_done.num = 0;
			da_push_back(move_source->filters_done,
				     &move_source->source);
			return;
		}
		if (next_move_on != NEXT_MOVE_ON_HOTKEY) {
			da_push_back(move_source->filters_done, &filter);
		}
		filter = obs_source_get_filter_by_name(parent, next_move_name);
		if (!filter && source) {
			filter = obs_source_get_filter_by_name(source,
							       next_move_name);
		}

		if (filter && strcmp(obs_source_get_unversioned_id(filter),
				     MOVE_SOURCE_FILTER_ID) == 0) {
			struct move_source_info *filter_data =
				obs_obj_get_data(filter);
			parent = obs_filter_get_parent(filter);
			source = obs_sceneitem_get_source(
				filter_data->scene_item);
			next_move_name = filter_data->next_move_name;
			next_move_on = filter_data->next_move_on;

		} else if (filter &&
			   (strcmp(obs_source_get_unversioned_id(filter),
				   MOVE_VALUE_FILTER_ID) == 0 ||
			    strcmp(obs_source_get_unversioned_id(filter),
				   MOVE_AUDIO_VALUE_FILTER_ID) == 0)) {
			struct move_value_info *filter_data =
				obs_obj_get_data(filter);
			parent = obs_filter_get_parent(filter);
			source = NULL;
			next_move_name = filter_data->next_move_name;
			next_move_on = filter_data->next_move_on;

		} else {
			obs_source_release(filter);
			move_source_start(move_source);
			move_source->filters_done.num = 0;
			da_push_back(move_source->filters_done,
				     &move_source->source);
			return;
		}
		obs_source_release(filter);
		i++;
	}
	for (i = 0; i < move_source->filters_done.num; i++) {
		if (move_source->filters_done.array[i] == filter) {
			move_source_start(move_source);
			move_source->filters_done.num = 0;
			da_push_back(move_source->filters_done,
				     &move_source->source);
			return;
		}
	}
	if (strcmp(obs_source_get_unversioned_id(filter),
		   MOVE_SOURCE_FILTER_ID) == 0) {
		move_source_start(obs_obj_get_data(filter));

	} else if (strcmp(obs_source_get_unversioned_id(filter),
			  MOVE_VALUE_FILTER_ID) == 0 ||
		   strcmp(obs_source_get_unversioned_id(filter),
			  MOVE_AUDIO_VALUE_FILTER_ID) == 0) {
		move_value_start(obs_obj_get_data(filter));
	}
	da_push_back(move_source->filters_done, &filter);

	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
}

void move_source_source_activate(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	if (move_source->start_trigger == START_TRIGGER_SOURCE_ACTIVATE)
		move_source_start(move_source);
	UNUSED_PARAMETER(call_data);
}

void move_source_source_deactivate(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	if (move_source->start_trigger == START_TRIGGER_SOURCE_DEACTIVATE)
		move_source_start(move_source);
	UNUSED_PARAMETER(call_data);
}

void move_source_source_show(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	if (move_source->start_trigger == START_TRIGGER_SOURCE_SHOW)
		move_source_start(move_source);
	UNUSED_PARAMETER(call_data);
}

void move_source_source_hide(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	if (move_source->start_trigger == START_TRIGGER_SOURCE_HIDE)
		move_source_start(move_source);
	UNUSED_PARAMETER(call_data);
}

void move_source_update(void *data, obs_data_t *settings)
{
	struct move_source_info *move_source = data;
	obs_source_t *parent = obs_filter_get_parent(move_source->source);
	obs_scene_t *scene = obs_scene_from_source(parent);
	const char *source_name = obs_data_get_string(settings, S_SOURCE);
	if (!move_source->source_name ||
	    strcmp(move_source->source_name, source_name) != 0) {
		obs_source_t *source =
			move_source->source_name &&
					strlen(move_source->source_name)
				? obs_get_source_by_name(
					  move_source->source_name)
				: NULL;
		if (source) {
			signal_handler_t *sh =
				obs_source_get_signal_handler(source);
			if (sh) {
				signal_handler_disconnect(
					sh, "activate",
					move_source_source_activate, data);
				signal_handler_disconnect(
					sh, "deactivate",
					move_source_source_deactivate, data);
				signal_handler_disconnect(
					sh, "show", move_source_source_show,
					data);
				signal_handler_disconnect(
					sh, "hide", move_source_source_hide,
					data);
			}
			obs_source_release(source);
		}

		bfree(move_source->source_name);
		move_source->source_name = NULL;

		source = obs_get_source_by_name(source_name);
		if (source) {
			signal_handler_t *sh =
				obs_source_get_signal_handler(source);
			if (sh) {
				signal_handler_connect(
					sh, "activate",
					move_source_source_activate, data);
				signal_handler_connect(
					sh, "deactivate",
					move_source_source_deactivate, data);
				signal_handler_connect(sh, "show",
						       move_source_source_show,
						       data);
				signal_handler_connect(sh, "hide",
						       move_source_source_hide,
						       data);
				move_source->source_name = bstrdup(source_name);
			}
			obs_source_release(source);
		}

		obs_sceneitem_release(move_source->scene_item);
		move_source->scene_item = NULL;
		if (move_source->source_name)
			obs_scene_enum_items(scene, find_sceneitem, data);
	}
	const char *filter_name = obs_source_get_name(move_source->source);
	if (!move_source->filter_name ||
	    strcmp(move_source->filter_name, filter_name) != 0) {
		bfree(move_source->filter_name);
		move_source->filter_name = NULL;
		if (move_source->move_start_hotkey != OBS_INVALID_HOTKEY_ID) {
			obs_hotkey_unregister(move_source->move_start_hotkey);
			move_source->move_start_hotkey = OBS_INVALID_HOTKEY_ID;
		}
		if (parent) {
			move_source->filter_name = bstrdup(filter_name);
			move_source->move_start_hotkey =
				obs_hotkey_register_source(
					parent, move_source->filter_name,
					move_source->filter_name,
					move_source_start_hotkey, data);
		}
	}
	move_source->change_visibility =
		obs_data_get_int(settings, S_CHANGE_VISIBILITY);
	move_source->duration = obs_data_get_int(settings, S_DURATION);
	move_source->start_delay = obs_data_get_int(settings, S_START_DELAY);
	move_source->end_delay = obs_data_get_int(settings, S_END_DELAY);
	move_source->curve =
		(float)obs_data_get_double(settings, S_CURVE_MATCH);
	move_source->easing = obs_data_get_int(settings, S_EASING_MATCH);
	move_source->easing_function =
		obs_data_get_int(settings, S_EASING_FUNCTION_MATCH);
	move_source->relative =
		obs_data_get_bool(settings, S_TRANSFORM_RELATIVE);
	if (!move_source->relative) {
		move_source->rot_to =
			(float)obs_data_get_double(settings, S_ROT);
		obs_data_get_vec2(settings, S_POS, &move_source->pos_to);
		obs_data_get_vec2(settings, S_SCALE, &move_source->scale_to);
		obs_data_get_vec2(settings, S_BOUNDS, &move_source->bounds_to);
		move_source->crop_to.left =
			(int)obs_data_get_int(settings, S_CROP_LEFT);
		move_source->crop_to.top =
			(int)obs_data_get_int(settings, S_CROP_TOP);
		move_source->crop_to.right =
			(int)obs_data_get_int(settings, S_CROP_RIGHT);
		move_source->crop_to.bottom =
			(int)obs_data_get_int(settings, S_CROP_BOTTOM);
	}
	move_source->start_trigger =
		(uint32_t)obs_data_get_int(settings, S_START_TRIGGER);

	const char *next_move_name = obs_data_get_string(settings, S_NEXT_MOVE);
	if (!move_source->next_move_name ||
	    strcmp(move_source->next_move_name, next_move_name) != 0) {
		bfree(move_source->next_move_name);
		move_source->next_move_name = bstrdup(next_move_name);
	}
	move_source->next_move_on = obs_data_get_int(settings, S_NEXT_MOVE_ON);

	move_source->change_order = obs_data_get_int(settings, S_CHANGE_ORDER);
	move_source->order_position =
		obs_data_get_int(settings, S_ORDER_POSITION);
}

void update_transform_text(obs_data_t *settings)
{
	struct vec2 pos;
	obs_data_get_vec2(settings, S_POS, &pos);
	const double rot = obs_data_get_double(settings, S_ROT);
	struct vec2 scale;
	obs_data_get_vec2(settings, S_SCALE, &scale);
	struct vec2 bounds;
	obs_data_get_vec2(settings, S_BOUNDS, &bounds);
	char transform_text[500];
	snprintf(
		transform_text, 500,
		"pos: x %.0f y %.0f rot: %.1f scale: x %.3f y %.3f bounds: x %.0f y %.0f crop: l %d t %d r %d b %d",
		pos.x, pos.y, rot, scale.x, scale.y, bounds.x, bounds.y,
		(int)obs_data_get_int(settings, S_CROP_LEFT),
		(int)obs_data_get_int(settings, S_CROP_TOP),
		(int)obs_data_get_int(settings, S_CROP_RIGHT),
		(int)obs_data_get_int(settings, S_CROP_BOTTOM));
	obs_data_set_string(settings, S_TRANSFORM_TEXT, transform_text);
	return;
}

void move_source_load(void *data, obs_data_t *settings)
{
	struct move_source_info *move_source = data;
	move_source_update(move_source, settings);
	update_transform_text(settings);
}

void move_source_source_rename(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	const char *new_name = calldata_string(call_data, "new_name");
	const char *prev_name = calldata_string(call_data, "prev_name");
	obs_data_t *settings = obs_source_get_settings(move_source->source);
	if (!settings || !new_name || !prev_name)
		return;
	const char *source_name = obs_data_get_string(settings, S_SOURCE);
	if (source_name && strlen(source_name) &&
	    strcmp(source_name, prev_name) == 0) {
		obs_data_set_string(settings, S_SOURCE, new_name);
	}
	obs_data_release(settings);
}

static void *move_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct move_source_info *move_source =
		bzalloc(sizeof(struct move_source_info));
	move_source->source = source;
	move_source->move_start_hotkey = OBS_INVALID_HOTKEY_ID;
	move_source_update(move_source, settings);
	signal_handler_connect(obs_get_signal_handler(), "source_rename",
			       move_source_source_rename, move_source);
	return move_source;
}

static void move_source_destroy(void *data)
{
	struct move_source_info *move_source = data;
	signal_handler_disconnect(obs_get_signal_handler(), "source_rename",
				  move_source_source_rename, move_source);

	obs_source_t *source = NULL;
	if (move_source->scene_item) {
		source = obs_sceneitem_get_source(move_source->scene_item);
	}
	if (!source && move_source->source_name &&
	    strlen(move_source->source_name)) {
		source = obs_get_source_by_name(move_source->source_name);
		obs_source_release(source);
	}
	if (source) {
		signal_handler_t *sh = obs_source_get_signal_handler(source);
		if (sh) {
			signal_handler_disconnect(sh, "activate",
						  move_source_source_activate,
						  data);
			signal_handler_disconnect(sh, "deactivate",
						  move_source_source_deactivate,
						  data);
			signal_handler_disconnect(
				sh, "show", move_source_source_show, data);
			signal_handler_disconnect(
				sh, "hide", move_source_source_hide, data);
		}
	}
	obs_sceneitem_release(move_source->scene_item);
	if (move_source->move_start_hotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(move_source->move_start_hotkey);

	bfree(move_source->source_name);
	bfree(move_source->filter_name);
	bfree(move_source->next_move_name);
	da_free(move_source->filters_done);
	bfree(move_source);
}

bool move_source_get_transform(obs_properties_t *props,
			       obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	struct move_source_info *move_source = data;
	bool settings_changed = false;
	if (!move_source->scene_item && move_source->source_name &&
	    strlen(move_source->source_name)) {
		obs_source_t *parent =
			obs_filter_get_parent(move_source->source);
		if (parent) {
			obs_scene_t *scene = obs_scene_from_source(parent);
			if (scene)
				obs_scene_enum_items(scene, find_sceneitem,
						     data);
		}
	}
	if (!move_source->scene_item)
		return settings_changed;
	settings_changed = true;
	obs_data_t *settings = obs_source_get_settings(move_source->source);
	const float rot = obs_sceneitem_get_rot(move_source->scene_item);
	struct vec2 pos;
	obs_sceneitem_get_pos(move_source->scene_item, &pos);
	struct vec2 scale;
	obs_sceneitem_get_scale(move_source->scene_item, &scale);
	struct vec2 bounds;
	obs_sceneitem_get_bounds(move_source->scene_item, &bounds);
	struct obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(move_source->scene_item, &crop);
	if (move_source->relative) {
		obs_data_set_double(settings, S_ROT, rot - move_source->rot_to);

		pos.x -= move_source->pos_to.x;
		pos.y -= move_source->pos_to.y;
		obs_data_set_vec2(settings, S_POS, &pos);
		scale.x -= move_source->scale_to.x;
		scale.y -= move_source->scale_to.y;
		obs_data_set_vec2(settings, S_SCALE, &scale);
		bounds.x -= move_source->bounds_to.x;
		bounds.y -= move_source->bounds_to.y;
		obs_data_set_vec2(settings, S_BOUNDS, &bounds);
		crop.left -= move_source->crop_to.left;
		obs_data_set_int(settings, S_CROP_LEFT, crop.left);
		crop.top -= move_source->crop_to.top;
		obs_data_set_int(settings, S_CROP_TOP, crop.top);
		crop.right -= move_source->crop_to.right;
		obs_data_set_int(settings, S_CROP_RIGHT, crop.right);
		crop.bottom -= move_source->crop_to.bottom;
		obs_data_set_int(settings, S_CROP_BOTTOM, crop.bottom);
	} else {
		obs_data_set_double(
			settings, S_ROT,
			obs_sceneitem_get_rot(move_source->scene_item));
		obs_data_set_vec2(settings, S_POS, &pos);
		obs_data_set_vec2(settings, S_SCALE, &scale);
		obs_data_set_vec2(settings, S_BOUNDS, &bounds);
		obs_data_set_int(settings, S_CROP_LEFT, crop.left);
		obs_data_set_int(settings, S_CROP_TOP, crop.top);
		obs_data_set_int(settings, S_CROP_RIGHT, crop.right);
		obs_data_set_int(settings, S_CROP_BOTTOM, crop.bottom);
	}
	move_source_update(data, settings);
	update_transform_text(settings);
	obs_data_release(settings);

	return settings_changed;
}

void prop_list_add_move_source_filter(obs_source_t *parent, obs_source_t *child,
				      void *data)
{
	UNUSED_PARAMETER(parent);
	if (strcmp(obs_source_get_unversioned_id(child),
		   MOVE_SOURCE_FILTER_ID) != 0 &&
	    strcmp(obs_source_get_unversioned_id(child),
		   MOVE_VALUE_FILTER_ID) != 0 &&
	    strcmp(obs_source_get_unversioned_id(child),
		   MOVE_AUDIO_VALUE_FILTER_ID) != 0)
		return;
	obs_property_t *p = data;
	const char *name = obs_source_get_name(child);
	obs_property_list_add_string(p, name, name);
}

bool move_source_changed(void *data, obs_properties_t *props,
			 obs_property_t *property, obs_data_t *settings)
{
	struct move_source_info *move_source = data;
	bool refresh = false;

	const char *source_name = obs_data_get_string(settings, S_SOURCE);
	if (move_source->source_name &&
	    strcmp(move_source->source_name, source_name) == 0)
		return refresh;
	bfree(move_source->source_name);
	move_source->source_name = bstrdup(source_name);
	obs_sceneitem_release(move_source->scene_item);
	move_source->scene_item = NULL;
	obs_source_t *parent = obs_filter_get_parent(move_source->source);
	if (parent) {
		obs_scene_t *scene = obs_scene_from_source(parent);
		if (scene)
			obs_scene_enum_items(scene, find_sceneitem, data);
	}
	obs_property_t *p = obs_properties_get(props, S_NEXT_MOVE);
	if (p) {
		obs_property_list_clear(p);
		obs_property_list_add_string(
			p, obs_module_text("NextMove.None"), "");
		obs_property_list_add_string(
			p, obs_module_text("NextMove.Reverse"),
			NEXT_MOVE_REVERSE);
		obs_source_enum_filters(parent,
					prop_list_add_move_source_filter, p);
		obs_source_t *source =
			obs_sceneitem_get_source(move_source->scene_item);
		if (source)
			obs_source_enum_filters(
				source, prop_list_add_move_source_filter, p);
	}
	refresh = move_source_get_transform(props, property, data);
	return refresh;
}

bool prop_list_add_source(obs_scene_t *scene, obs_sceneitem_t *item,
			  void *data);
void prop_list_add_easings(obs_property_t *p);
void prop_list_add_easing_functions(obs_property_t *p);

bool move_source_transform_text_changed(void *data, obs_properties_t *props,
					obs_property_t *property,
					obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	UNUSED_PARAMETER(data);
	const char *transform_text =
		obs_data_get_string(settings, S_TRANSFORM_TEXT);
	struct vec2 pos;
	float rot;
	struct vec2 scale;
	struct vec2 bounds;
	struct obs_sceneitem_crop crop;
	if (sscanf(transform_text,
		   "pos: x %f y %f rot: %f scale: x %f y %f bounds: x %f y %f crop: l %d t %d r %d b %d",
		   &pos.x, &pos.y, &rot, &scale.x, &scale.y, &bounds.x,
		   &bounds.y, &crop.left, &crop.top, &crop.right,
		   &crop.bottom) != 11) {
		update_transform_text(settings);
		return true;
	}
	obs_data_set_vec2(settings, S_POS, &pos);
	obs_data_set_double(settings, S_ROT, rot);
	obs_data_set_vec2(settings, S_SCALE, &scale);
	obs_data_set_vec2(settings, S_BOUNDS, &bounds);
	obs_data_set_int(settings, S_CROP_LEFT, crop.left);
	obs_data_set_int(settings, S_CROP_TOP, crop.top);
	obs_data_set_int(settings, S_CROP_RIGHT, crop.right);
	obs_data_set_int(settings, S_CROP_BOTTOM, crop.bottom);
	return false;
}

bool move_source_transform_relative_changed(void *data, obs_properties_t *props,
					    obs_property_t *property,
					    obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	struct move_source_info *move_source = data;
	const bool relative = obs_data_get_bool(settings, S_TRANSFORM_RELATIVE);
	if (relative == move_source->relative)
		return false;

	move_source->relative = relative;

	if (!move_source->scene_item && move_source->source_name &&
	    strlen(move_source->source_name)) {
		obs_source_t *parent =
			obs_filter_get_parent(move_source->source);
		if (parent) {
			obs_scene_t *scene = obs_scene_from_source(parent);
			if (scene)
				obs_scene_enum_items(scene, find_sceneitem,
						     data);
		}
	}
	struct vec2 pos;

	struct vec2 scale;

	struct vec2 bounds;
	if (relative) {
		obs_data_set_double(
			settings, S_ROT,
			move_source->rot_to -
				obs_sceneitem_get_rot(move_source->scene_item));
		obs_sceneitem_get_pos(move_source->scene_item, &pos);
		pos.x -= move_source->pos_to.x;
		pos.y -= move_source->pos_to.y;
		obs_data_set_vec2(settings, S_POS, &pos);
		obs_sceneitem_get_scale(move_source->scene_item, &scale);
		scale.x -= move_source->scale_to.x;
		scale.y -= move_source->scale_to.y;
		obs_data_set_vec2(settings, S_SCALE, &scale);
		obs_sceneitem_get_bounds(move_source->scene_item, &bounds);
		bounds.x -= move_source->bounds_to.x;
		bounds.y -= move_source->bounds_to.y;
		obs_data_set_vec2(settings, S_BOUNDS, &bounds);
		struct obs_sceneitem_crop crop;
		obs_sceneitem_get_crop(move_source->scene_item, &crop);
		obs_data_set_int(settings, S_CROP_LEFT,
				 crop.left - move_source->crop_to.left);
		obs_data_set_int(settings, S_CROP_TOP,
				 crop.top - move_source->crop_to.top);
		obs_data_set_int(settings, S_CROP_RIGHT,
				 crop.right - move_source->crop_to.right);
		obs_data_set_int(settings, S_CROP_BOTTOM,
				 crop.bottom - move_source->crop_to.bottom);
	} else {
		move_source->rot_to +=
			(float)obs_data_get_double(settings, S_ROT);
		obs_data_set_double(settings, S_ROT, move_source->rot_to);
		obs_data_get_vec2(settings, S_POS, &pos);
		move_source->pos_to.x += pos.x;
		move_source->pos_to.y += pos.y;
		obs_data_set_vec2(settings, S_POS, &move_source->pos_to);
		obs_data_get_vec2(settings, S_SCALE, &scale);
		move_source->scale_to.x += scale.x;
		move_source->scale_to.y += scale.y;
		obs_data_set_vec2(settings, S_SCALE, &move_source->scale_to);
		obs_data_get_vec2(settings, S_BOUNDS, &bounds);
		move_source->bounds_to.x += bounds.x;
		move_source->bounds_to.y += bounds.y;
		obs_data_set_vec2(settings, S_BOUNDS, &move_source->bounds_to);
		move_source->crop_to.left +=
			(int)obs_data_get_int(settings, S_CROP_LEFT);
		if (move_source->crop_to.left < 0)
			move_source->crop_to.left = 0;
		obs_data_set_int(settings, S_CROP_LEFT,
				 move_source->crop_to.left);
		move_source->crop_to.top +=
			(int)obs_data_get_int(settings, S_CROP_TOP);
		if (move_source->crop_to.top < 0)
			move_source->crop_to.top = 0;
		obs_data_set_int(settings, S_CROP_TOP,
				 move_source->crop_to.top);
		move_source->crop_to.right +=
			(int)obs_data_get_int(settings, S_CROP_RIGHT);
		if (move_source->crop_to.right < 0)
			move_source->crop_to.right = 0;
		obs_data_set_int(settings, S_CROP_RIGHT,
				 move_source->crop_to.right);
		move_source->crop_to.bottom +=
			(int)obs_data_get_int(settings, S_CROP_BOTTOM);
		if (move_source->crop_to.bottom < 0)
			move_source->crop_to.bottom = 0;
		obs_data_set_int(settings, S_CROP_BOTTOM,
				 move_source->crop_to.bottom);
	}
	update_transform_text(settings);
	return true;
}

static obs_properties_t *move_source_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();
	struct move_source_info *move_source = data;
	obs_source_t *parent = obs_filter_get_parent(move_source->source);
	obs_scene_t *scene = obs_scene_from_source(parent);
	if (!scene) {
		obs_properties_add_button(ppts, "warning",
					  obs_module_text("ScenesOnlyFilter"),
					  NULL);
		return ppts;
	}
	if (!move_source->scene_item && move_source->source_name &&
	    strlen(move_source->source_name)) {
		obs_scene_enum_items(scene, find_sceneitem, move_source);
	}
	obs_property_t *p = obs_properties_add_list(ppts, S_SOURCE,
						    obs_module_text("Source"),
						    OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_STRING);
	obs_scene_enum_items(scene, prop_list_add_source, p);
	obs_property_set_modified_callback2(p, move_source_changed, data);

	p = obs_properties_add_bool(ppts, S_TRANSFORM_RELATIVE,
				    obs_module_text("TransformRelative"));
	obs_property_set_modified_callback2(
		p, move_source_transform_relative_changed, data);

	p = obs_properties_add_text(ppts, S_TRANSFORM_TEXT,
				    obs_module_text("Transform"),
				    OBS_TEXT_DEFAULT);
	obs_property_set_modified_callback2(
		p, move_source_transform_text_changed, data);
	obs_properties_add_button(ppts, "transform_get",
				  obs_module_text("GetTransform"),
				  move_source_get_transform);

	p = obs_properties_add_list(ppts, S_CHANGE_VISIBILITY,
				    obs_module_text("ChangeVisibility"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.No"),
				  CHANGE_VISIBILITY_NONE);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.Show"),
				  CHANGE_VISIBILITY_SHOW);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.Hide"),
				  CHANGE_VISIBILITY_HIDE);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.Toggle"),
				  CHANGE_VISIBILITY_TOGGLE);

	p = obs_properties_add_list(ppts, S_CHANGE_ORDER,
				    obs_module_text("ChangeOrder"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("ChangeOrder.No"),
				  CHANGE_ORDER_NONE);
	obs_property_list_add_int(p,
				  obs_module_text("ChangeOrder.StartAbsolute"),
				  CHANGE_ORDER_START | CHANGE_ORDER_ABSOLUTE);
	obs_property_list_add_int(p, obs_module_text("ChangeOrder.EndAbsolute"),
				  CHANGE_ORDER_END | CHANGE_ORDER_ABSOLUTE);
	obs_property_list_add_int(p,
				  obs_module_text("ChangeOrder.StartRelative"),
				  CHANGE_ORDER_START | CHANGE_ORDER_RELATIVE);
	obs_property_list_add_int(p, obs_module_text("ChangeOrder.EndRelative"),
				  CHANGE_ORDER_END | CHANGE_ORDER_RELATIVE);
	p = obs_properties_add_int(ppts, S_ORDER_POSITION,
				   obs_module_text("OrderPosition"), -1000,
				   1000, 1);

	p = obs_properties_add_int(ppts, S_START_DELAY,
				   obs_module_text("StartDelay"), 0, 10000000,
				   100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_int(
		ppts, S_DURATION, obs_module_text("Duration"), 10, 100000, 100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_int(ppts, S_END_DELAY,
				   obs_module_text("EndDelay"), 0, 10000000,
				   100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_list(ppts, S_EASING_MATCH,
				    obs_module_text("Easing"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);

	p = obs_properties_add_list(ppts, S_EASING_FUNCTION_MATCH,
				    obs_module_text("EasingFunction"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

	obs_properties_add_float_slider(
		ppts, S_CURVE_MATCH, obs_module_text("Curve"), -2.0, 2.0, 0.01);

	p = obs_properties_add_list(ppts, S_START_TRIGGER,
				    obs_module_text("StartTrigger"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("StartTrigger.None"),
				  START_TRIGGER_NONE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Activate"),
				  START_TRIGGER_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Deactivate"),
				  START_TRIGGER_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Show"),
				  START_TRIGGER_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Hide"),
				  START_TRIGGER_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Enable"),
				  START_TRIGGER_ENABLE);
	obs_property_list_add_int(p,
				  obs_module_text("StartTrigger.EnableDisable"),
				  START_TRIGGER_ENABLE_DISABLE);
	obs_property_list_add_int(
		p, obs_module_text("StartTrigger.SourceActivate"),
		START_TRIGGER_SOURCE_ACTIVATE);
	obs_property_list_add_int(
		p, obs_module_text("StartTrigger.SourceDeactivate"),
		START_TRIGGER_SOURCE_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceShow"),
				  START_TRIGGER_SOURCE_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceHide"),
				  START_TRIGGER_SOURCE_HIDE);

	p = obs_properties_add_list(ppts, S_NEXT_MOVE,
				    obs_module_text("NextMove"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("NextMove.None"), "");
	obs_property_list_add_string(p, obs_module_text("NextMove.Reverse"),
				     NEXT_MOVE_REVERSE);
	obs_source_enum_filters(parent, prop_list_add_move_source_filter, p);
	obs_source_t *source =
		obs_sceneitem_get_source(move_source->scene_item);
	if (source)
		obs_source_enum_filters(source,
					prop_list_add_move_source_filter, p);

	p = obs_properties_add_list(ppts, S_NEXT_MOVE_ON,
				    obs_module_text("NextMoveOn"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("NextMoveOn.End"),
				  NEXT_MOVE_ON_END);
	obs_property_list_add_int(p, obs_module_text("NextMoveOn.Hotkey"),
				  NEXT_MOVE_ON_HOTKEY);

	obs_properties_add_button(ppts, "move_source_start",
				  obs_module_text("Start"),
				  move_source_start_button);
	return ppts;
}

void move_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, S_DURATION, 300);
	obs_data_set_default_int(settings, S_EASING_MATCH, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_FUNCTION_MATCH,
				 EASING_CUBIC);
	obs_data_set_default_double(settings, S_CURVE_MATCH, 0.0);
}

void move_source_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct move_source_info *filter = data;
	obs_source_skip_video_filter(filter->source);
}

static const char *move_source_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("MoveSourceFilter");
}

float get_eased(float f, long long easing, long long easing_function);
void vec2_bezier(struct vec2 *dst, struct vec2 *begin, struct vec2 *control,
		 struct vec2 *end, const float t);

void move_source_tick(void *data, float seconds)
{
	struct move_source_info *move_source = data;
	const bool enabled = obs_source_enabled(move_source->source);
	if (move_source->enabled != enabled) {
		if (enabled && move_source->start_trigger ==
				       START_TRIGGER_ENABLE ||
		    move_source->start_trigger == START_TRIGGER_ENABLE_DISABLE)
			move_source_start(move_source);
		move_source->enabled = enabled;
	}
	if (!move_source->moving || !enabled)
		return;

	if (!move_source->scene_item || !move_source->duration) {
		move_source->moving = false;
		return;
	}
	move_source->running_duration += seconds;
	if (move_source->running_duration * 1000.0f <
	    (move_source->reverse ? move_source->end_delay
				  : move_source->start_delay)) {
		if (!move_source->reverse) {
			move_source->rot_from =
				obs_sceneitem_get_rot(move_source->scene_item);
			obs_sceneitem_get_pos(move_source->scene_item,
					      &move_source->pos_from);
			obs_sceneitem_get_scale(move_source->scene_item,
						&move_source->scale_from);
			obs_sceneitem_get_bounds(move_source->scene_item,
						 &move_source->bounds_from);
			obs_sceneitem_get_crop(move_source->scene_item,
					       &move_source->crop_from);
			if (move_source->relative) {
				calc_relative_to(move_source, 1.0f);
			}
		} else if (move_source->relative) {
			calc_relative_to(move_source, -1.0f);
		}
		return;
	}
	if (move_source->running_duration * 1000.0f >=
	    (float)(move_source->start_delay + move_source->duration +
		    move_source->end_delay)) {
		move_source->moving = false;
	}
	float t = (move_source->running_duration * 1000.0f -
		   (float)(move_source->reverse ? move_source->end_delay
						: move_source->start_delay)) /
		  (float)move_source->duration;
	if (t >= 1.0f) {
		t = 1.0f;
	}
	if (move_source->reverse) {
		t = 1.0f - t;
	}
	t = get_eased(t, move_source->easing, move_source->easing_function);

	float ot = t;
	if (t > 1.0f)
		ot = 1.0f;
	else if (t < 0.0f)
		ot = 0.0f;

	struct vec2 pos;
	if (move_source->curve != 0.0f) {
		const float diff_x =
			fabsf(move_source->pos_from.x - move_source->pos_to.x);
		const float diff_y =
			fabsf(move_source->pos_from.y - move_source->pos_to.y);
		struct vec2 control_pos;
		vec2_set(&control_pos,
			 0.5f * move_source->pos_from.x +
				 0.5f * move_source->pos_to.x,
			 0.5f * move_source->pos_from.y +
				 0.5f * move_source->pos_to.y);
		if (control_pos.x >= (move_source->canvas_width >> 1)) {
			control_pos.x += diff_y * move_source->curve;
		} else {
			control_pos.x -= diff_y * move_source->curve;
		}
		if (control_pos.y >= (move_source->canvas_height >> 1)) {
			control_pos.y += diff_x * move_source->curve;
		} else {
			control_pos.y -= diff_x * move_source->curve;
		}
		vec2_bezier(&pos, &move_source->pos_from, &control_pos,
			    &move_source->pos_to, t);
	} else {
		vec2_set(&pos,
			 (1.0f - t) * move_source->pos_from.x +
				 t * move_source->pos_to.x,
			 (1.0f - t) * move_source->pos_from.y +
				 t * move_source->pos_to.y);
	}
	obs_sceneitem_defer_update_begin(move_source->scene_item);
	obs_sceneitem_set_pos(move_source->scene_item, &pos);
	const float rot =
		(1.0f - t) * move_source->rot_from + t * move_source->rot_to;
	obs_sceneitem_set_rot(move_source->scene_item, rot);
	struct vec2 scale;
	vec2_set(&scale,
		 (1.0f - t) * move_source->scale_from.x +
			 t * move_source->scale_to.x,
		 (1.0f - t) * move_source->scale_from.y +
			 t * move_source->scale_to.y);
	obs_sceneitem_set_scale(move_source->scene_item, &scale);
	struct vec2 bounds;
	vec2_set(&bounds,
		 (1.0f - t) * move_source->bounds_from.x +
			 t * move_source->bounds_to.x,
		 (1.0f - t) * move_source->bounds_from.y +
			 t * move_source->bounds_to.y);
	obs_sceneitem_set_bounds(move_source->scene_item, &bounds);
	struct obs_sceneitem_crop crop;
	crop.left =
		(int)((float)(1.0f - ot) * (float)move_source->crop_from.left +
		      ot * (float)move_source->crop_to.left);
	crop.top =
		(int)((float)(1.0f - ot) * (float)move_source->crop_from.top +
		      ot * (float)move_source->crop_to.top);
	crop.right =
		(int)((float)(1.0f - ot) * (float)move_source->crop_from.right +
		      ot * (float)move_source->crop_to.right);
	crop.bottom = (int)((float)(1.0f - ot) *
				    (float)move_source->crop_from.bottom +
			    ot * (float)move_source->crop_to.bottom);
	obs_sceneitem_set_crop(move_source->scene_item, &crop);
	obs_sceneitem_defer_update_end(move_source->scene_item);
	if (!move_source->moving) {
		if (move_source->start_trigger ==
			    START_TRIGGER_ENABLE_DISABLE &&
		    (move_source->reverse || !move_source->next_move_name ||
		     strcmp(move_source->next_move_name, NEXT_MOVE_REVERSE) !=
			     0)) {
			obs_source_set_enabled(move_source->source, false);
		}
		if (move_source->change_visibility == CHANGE_VISIBILITY_HIDE) {
			obs_sceneitem_set_visible(move_source->scene_item,
						  false);
		} else if (move_source->change_visibility ==
				   CHANGE_VISIBILITY_TOGGLE &&
			   !move_source->visibility_toggled) {
			obs_sceneitem_set_visible(move_source->scene_item,
						  false);
		}
		if ((move_source->change_order & CHANGE_ORDER_END) != 0) {
			if ((move_source->change_order &
			     CHANGE_ORDER_RELATIVE) != 0 &&
			    move_source->order_position) {
				if (move_source->order_position > 0) {
					for (int i = 0;
					     i < move_source->order_position;
					     i++) {
						obs_sceneitem_set_order(
							move_source->scene_item,
							OBS_ORDER_MOVE_UP);
					}
				} else if (move_source->order_position < 0) {
					for (int i = 0;
					     i > move_source->order_position;
					     i--) {
						obs_sceneitem_set_order(
							move_source->scene_item,
							OBS_ORDER_MOVE_DOWN);
					}
				}
			} else if ((move_source->change_order &
				    CHANGE_ORDER_ABSOLUTE) != 0) {
				obs_sceneitem_set_order_position(
					move_source->scene_item,
					move_source->order_position);
			}
		}
		if (move_source->next_move_on == NEXT_MOVE_ON_END &&
		    move_source->next_move_name &&
		    strlen(move_source->next_move_name) &&
		    (!move_source->filter_name ||
		     strcmp(move_source->filter_name,
			    move_source->next_move_name) != 0)) {
			if (strcmp(move_source->next_move_name,
				   NEXT_MOVE_REVERSE) == 0) {
				move_source->reverse = !move_source->reverse;
				if (move_source->reverse)
					move_source_start(move_source);
			} else {
				obs_source_t *parent = obs_filter_get_parent(
					move_source->source);
				if (parent) {
					obs_source_t *filter =
						obs_source_get_filter_by_name(
							parent,
							move_source
								->next_move_name);
					if (!filter) {
						filter = obs_source_get_filter_by_name(
							obs_sceneitem_get_source(
								move_source
									->scene_item),
							move_source
								->next_move_name);
					}
					if (filter) {
						if (strcmp(obs_source_get_unversioned_id(
								   filter),
							   MOVE_SOURCE_FILTER_ID) ==
						    0) {
							struct move_source_info *filter_data =
								obs_obj_get_data(
									filter);
							if (move_source->start_trigger ==
								    START_TRIGGER_ENABLE_DISABLE &&
							    !obs_source_enabled(
								    filter_data
									    ->source))
								obs_source_set_enabled(
									filter_data
										->source,
									true);
							move_source_start(
								filter_data);
						} else if (
							strcmp(obs_source_get_unversioned_id(
								       filter),
							       MOVE_VALUE_FILTER_ID) ==
								0 ||
							strcmp(obs_source_get_unversioned_id(
								       filter),
							       MOVE_AUDIO_VALUE_FILTER_ID) ==
								0) {
							struct move_value_info *filter_data =
								obs_obj_get_data(
									filter);
							if (move_source->start_trigger ==
								    START_TRIGGER_ENABLE_DISABLE &&
							    !obs_source_enabled(
								    filter_data
									    ->source))
								obs_source_set_enabled(
									filter_data
										->source,
									true);
							move_value_start(
								filter_data);
						}
						obs_source_release(filter);
					}
				}
			}
		} else if (move_source->next_move_on == NEXT_MOVE_ON_HOTKEY &&
			   move_source->next_move_name &&
			   strcmp(move_source->next_move_name,
				  NEXT_MOVE_REVERSE) == 0) {
			move_source->reverse = !move_source->reverse;
		}
	}
}

void move_source_activate(void *data)
{
	struct move_source_info *move_source = data;
	if (move_source->start_trigger == START_TRIGGER_ACTIVATE)
		move_source_start(move_source);
}

void move_source_deactivate(void *data)
{
	struct move_source_info *move_source = data;
	if (move_source->start_trigger == START_TRIGGER_DEACTIVATE)
		move_source_start(move_source);
}

void move_source_show(void *data)
{
	struct move_source_info *move_source = data;
	if (move_source->start_trigger == START_TRIGGER_SHOW)
		move_source_start(move_source);
}

void move_source_hide(void *data)
{
	struct move_source_info *move_source = data;
	if (move_source->start_trigger == START_TRIGGER_HIDE)
		move_source_start(move_source);
}

struct obs_source_info move_source_filter = {
	.id = MOVE_SOURCE_FILTER_ID,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = move_source_get_name,
	.create = move_source_create,
	.destroy = move_source_destroy,
	.get_properties = move_source_properties,
	.get_defaults = move_source_defaults,
	.video_render = move_source_video_render,
	.video_tick = move_source_tick,
	.update = move_source_update,
	.load = move_source_load,
	.activate = move_source_activate,
	.deactivate = move_source_deactivate,
	.show = move_source_show,
	.hide = move_source_hide,
};
