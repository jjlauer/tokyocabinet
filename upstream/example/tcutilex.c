#include <tcutil.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int main(int argc, char **argv){

  { /* example to use an extensible string object */
    TCXSTR *xstr;
    /* create the object */
    xstr = tcxstrnew();
    /* concatenate strings */
    tcxstrcat2(xstr, "hop");
    tcxstrcat2(xstr, "step");
    tcxstrcat2(xstr, "jump");
    /* print the size and the content */
    printf("%d:%s\n", tcxstrsize(xstr), tcxstrptr(xstr));
    /* delete the object */
    tcxstrdel(xstr);
  }

  { /* example to use a list object */
    TCLIST *list;
    int i;
    /* create the object */
    list = tclistnew();
    /* add strings to the tail */
    tclistpush2(list, "hop");
    tclistpush2(list, "step");
    tclistpush2(list, "jump");
    /* print all elements */
    for(i = 0; i < tclistnum(list); i++){
      printf("%d:%s\n", i, tclistval2(list, i));
    }
    /* delete the object */
    tclistdel(list);
  }

  { /* example to use a map object */
    TCMAP *map;
    const char *key;
    /* create the object */
    map = tcmapnew();
    /* add records */
    tcmapput2(map, "foo", "hop");
    tcmapput2(map, "bar", "step");
    tcmapput2(map, "baz", "jump");
    /* print all records */
    tcmapiterinit(map);
    while((key = tcmapiternext2(map)) != NULL){
      printf("%s:%s\n", key, tcmapget2(map, key));
    }
    /* delete the object */
    tcmapdel(map);
  }

  return 0;
}
