#include <plugin.h>
#include "srv.h"

int run_plugin_init( srv_t *srv ) 
{
  plugin_t *pl ;

  for( pl = srv->root->db->plugins ; pl ; pl = pl->next ) {
    if( strcmp(pl->name,"dback") == 0 ) continue ; 
 
    if( pl->init ) {
      traceLog( "Running the init function for %s", pl->name ) ;
      pl->init( conf_get, srv, &pl->dyn ) ;
    }
  }
  return 0 ;
}

