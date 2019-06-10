/*
 * Copyright (c) 2016 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stddef.h>

#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>
#include <l3xc/l3xc.h>
#include <vnet/mpls/mpls_types.h>
#include <vnet/fib/fib_path_list.h>
#include <vnet/fib/fib_api.h>

#include <vpp/app/version.h>

#include <vlibapi/api.h>
#include <vlibmemory/api.h>

/* define message IDs */
#include <l3xc/l3xc_msg_enum.h>

/* define message structures */
#define vl_typedefs
#include <l3xc/l3xc_all_api_h.h>
#undef vl_typedefs

/* define generated endian-swappers */
#define vl_endianfun
#include <l3xc/l3xc_all_api_h.h>
#undef vl_endianfun

/* instantiate all the print functions we know about */
#define vl_print(handle, ...) vlib_cli_output (handle, __VA_ARGS__)
#define vl_printfun
#include <l3xc/l3xc_all_api_h.h>
#undef vl_printfun

/* Get the API version number */
#define vl_api_version(n,v) static u32 api_version=(v);
#include <l3xc/l3xc_all_api_h.h>
#undef vl_api_version

/**
 * Base message ID fot the plugin
 */
static u32 l3xc_base_msg_id;

#include <vlibapi/api_helper_macros.h>

/* List of message types that this plugin understands */

#define foreach_l3xc_plugin_api_msg                     \
  _(L3XC_PLUGIN_GET_VERSION, l3xc_plugin_get_version)   \
  _(L3XC_UPDATE, l3xc_update)                           \
  _(L3XC_DEL, l3xc_del)                                 \
  _(L3XC_DUMP, l3xc_dump)

static void
vl_api_l3xc_plugin_get_version_t_handler (vl_api_l3xc_plugin_get_version_t *
					  mp)
{
  vl_api_l3xc_plugin_get_version_reply_t *rmp;
  vl_api_registration_t *rp;

  rp = vl_api_client_index_to_registration (mp->client_index);
  if (rp == 0)
    return;

  rmp = vl_msg_api_alloc (sizeof (*rmp));
  rmp->_vl_msg_id =
    ntohs (VL_API_L3XC_PLUGIN_GET_VERSION_REPLY + l3xc_base_msg_id);
  rmp->context = mp->context;
  rmp->major = htonl (L3XC_PLUGIN_VERSION_MAJOR);
  rmp->minor = htonl (L3XC_PLUGIN_VERSION_MINOR);

  vl_api_send_msg (rp, (u8 *) rmp);
}

static void
vl_api_l3xc_update_t_handler (vl_api_l3xc_update_t * mp)
{
  vl_api_l3xc_update_reply_t *rmp;
  fib_route_path_t *paths = NULL, *path;
  int rv = 0;
  u8 pi;

  VALIDATE_SW_IF_INDEX (&mp->l3xc);

  if (0 == mp->l3xc.n_paths)
    {
      rv = VNET_API_ERROR_INVALID_VALUE;
      goto done;
    }

  vec_validate (paths, mp->l3xc.n_paths - 1);

  for (pi = 0; pi < mp->l3xc.n_paths; pi++)
    {
      path = &paths[pi];
      rv = fib_path_api_parse (&mp->l3xc.paths[pi], path);

      if (0 != rv)
	{
	  goto done;
	}
    }

  rv = l3xc_update (ntohl (mp->l3xc.sw_if_index), mp->l3xc.is_ip6, paths);

done:
  vec_free (paths);

  BAD_SW_IF_INDEX_LABEL;

  /* *INDENT-OFF* */
  REPLY_MACRO2 (VL_API_L3XC_UPDATE_REPLY + l3xc_base_msg_id,
  ({
    rmp->stats_index = 0;
  }))
  /* *INDENT-ON* */
}

static void
vl_api_l3xc_del_t_handler (vl_api_l3xc_del_t * mp)
{
  vl_api_l3xc_del_reply_t *rmp;
  int rv = 0;

  VALIDATE_SW_IF_INDEX (mp);

  rv = l3xc_delete (ntohl (mp->sw_if_index), mp->is_ip6);

  BAD_SW_IF_INDEX_LABEL;

  REPLY_MACRO (VL_API_L3XC_DEL_REPLY + l3xc_base_msg_id);
}

typedef struct l3xc_dump_walk_ctx_t_
{
  vl_api_registration_t *rp;
  u32 context;
} l3xc_dump_walk_ctx_t;

static int
l3xc_send_details (u32 l3xci, void *args)
{
  fib_route_path_encode_t *api_rpaths = NULL, *api_rpath;
  vl_api_l3xc_details_t *mp;
  l3xc_dump_walk_ctx_t *ctx;
  vl_api_fib_path_t *fp;
  size_t msg_size;
  l3xc_t *l3xc;
  u8 n_paths;

  ctx = args;
  l3xc = l3xc_get (l3xci);
  n_paths = fib_path_list_get_n_paths (l3xc->l3xc_pl);
  msg_size = sizeof (*mp) + sizeof (mp->l3xc.paths[0]) * n_paths;

  mp = vl_msg_api_alloc (msg_size);
  clib_memset (mp, 0, msg_size);
  mp->_vl_msg_id = ntohs (VL_API_L3XC_DETAILS + l3xc_base_msg_id);

  /* fill in the message */
  mp->context = ctx->context;
  mp->l3xc.n_paths = n_paths;
  mp->l3xc.sw_if_index = htonl (l3xc->l3xc_sw_if_index);

  fib_path_list_walk_w_ext (l3xc->l3xc_pl, NULL, fib_path_encode,
			    &api_rpaths);

  fp = mp->l3xc.paths;
  vec_foreach (api_rpath, api_rpaths)
  {
    fib_api_path_encode (api_rpath, fp);
    fp++;
  }

  vl_api_send_msg (ctx->rp, (u8 *) mp);

  return (1);
}

static void
vl_api_l3xc_dump_t_handler (vl_api_l3xc_dump_t * mp)
{
  vl_api_registration_t *rp;
  u32 sw_if_index;

  rp = vl_api_client_index_to_registration (mp->client_index);
  if (rp == 0)
    return;

  l3xc_dump_walk_ctx_t ctx = {
    .rp = rp,
    .context = mp->context,
  };

  sw_if_index = ntohl (mp->sw_if_index);

  if (~0 == sw_if_index)
    l3xc_walk (l3xc_send_details, &ctx);
  else
    {
      fib_protocol_t fproto;
      index_t l3xci;

      FOR_EACH_FIB_IP_PROTOCOL (fproto)
      {
	l3xci = l3xc_find (sw_if_index, fproto);

	if (INDEX_INVALID != l3xci)
	  l3xc_send_details (l3xci, &ctx);
      }
    }
}

#define vl_msg_name_crc_list
#include <l3xc/l3xc_all_api_h.h>
#undef vl_msg_name_crc_list

/* Set up the API message handling tables */
static clib_error_t *
l3xc_plugin_api_hookup (vlib_main_t * vm)
{
#define _(N,n)                                                  \
    vl_msg_api_set_handlers((VL_API_##N + l3xc_base_msg_id),     \
                            #n,					\
                            vl_api_##n##_t_handler,             \
                            vl_noop_handler,                    \
                            vl_api_##n##_t_endian,              \
                            vl_api_##n##_t_print,               \
                            sizeof(vl_api_##n##_t), 1);
  foreach_l3xc_plugin_api_msg;
#undef _

  return 0;
}

static void
setup_message_id_table (api_main_t * apim)
{
#define _(id,n,crc) \
  vl_msg_api_add_msg_name_crc (apim, #n "_" #crc, id + l3xc_base_msg_id);
  foreach_vl_msg_name_crc_l3xc;
#undef _
}

static clib_error_t *
l3xc_api_init (vlib_main_t * vm)
{
  clib_error_t *error = 0;

  u8 *name = format (0, "l3xc_%08x%c", api_version, 0);

  /* Ask for a correctly-sized block of API message decode slots */
  l3xc_base_msg_id = vl_msg_api_get_msg_ids ((char *) name,
					     VL_MSG_FIRST_AVAILABLE);

  error = l3xc_plugin_api_hookup (vm);

  /* Add our API messages to the global name_crc hash table */
  setup_message_id_table (&api_main);

  vec_free (name);

  return error;
}

VLIB_INIT_FUNCTION (l3xc_api_init);

/* *INDENT-OFF* */
VLIB_PLUGIN_REGISTER () = {
    .version = VPP_BUILD_VER,
    .description = "L3 Cross-Connect (L3XC)",
};
/* *INDENT-ON* */

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
