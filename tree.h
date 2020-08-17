/* tree.h - declare structures used by tree.c
 * vix 27jun86 [broken out of tree.c]
 * vix 22jan93 [revisited; uses RCS, ANSI, POSIX; has bug fixes]
 *
 * $Id:$
 */


#ifndef _TREE_FLAG
#define _TREE_FLAG

typedef struct _TREENODE {
  struct _TREENODE *tree_l;
  struct _TREENODE *tree_r;
  int tree_b;
  void *tree_p;
} TREENODE;

typedef int (*TREECOMPFUNC)(void *k1, void *k2);
typedef int (*TREETRAVFUNC)(void *parm);
typedef void (*TREEDELFUNC)(void *parm);

void TreeInit(TREENODE **ppr_tree);

void *TreeSearch(TREENODE **ppr_tree, TREECOMPFUNC TreeComp, void *p_user);
void TreeAdd(TREENODE **ppr_tree, TREECOMPFUNC TreeComp, void *p_user, TREEDELFUNC TreeDeleteNode);
int TreeDelete(TREENODE **ppr_p, TREECOMPFUNC TreeComp, void *p_user, TREEDELFUNC TreeDeleteNode);
int TreeTraverse(TREENODE **ppr_tree, TREETRAVFUNC TreeTraverseNode);
void TreeDestroy(TREENODE **ppr_tree, TREEDELFUNC TreeDeleteNode);

#endif  /* _TREE_FLAG */
