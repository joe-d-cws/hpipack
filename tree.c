/* as_tree - tree library for as
 * vix 14dec85 [written]
 * vix 02feb86 [added tree balancing from wirth "a+ds=p" p. 220-221]
 * vix 06feb86 [added TreeDestroy()]
 * vix 20jun86 [added TreeDelete per wirth a+ds (mod2 v.) p. 224]
 * vix 23jun86 [added Delete2 uar to add for replaced nodes]
 * vix 22jan93 [revisited; uses RCS, ANSI, POSIX; has bug fixes]
 */


/* This program text was created by Paul Vixie using examples from the book:
 * "Algorithms & Data Structures," Niklaus Wirth, Prentice-Hall, 1986, ISBN
 * 0-13-022005-1. This code and associated documentation is hereby placed
 * in the public domain.
 */

#include <windows.h>
#include "tree.h"

#define FALSE 0
#define TRUE 1

HANDLE TreeHeap = NULL;
TREENODE *TreeArray = NULL;
int NextNode = 0;

static LPVOID GetMem(int size)
{
  return HeapAlloc(TreeHeap, 0, size); 
  //return GlobalAlloc(GPTR, size);
	//ZeroMemory(This, sizeof(DirEntry));
}

static void FreeMem(LPVOID x)
{
  //HeapFree(TreeHeap, 0, x);
  //GlobalFree(x);
}

TREENODE *NewNode(void)
{
  //return GetMem(sizeof(TREENODE));
  return TreeArray+(NextNode++);
}

static void Sprout(TREENODE **ppr, void *p_data, int *pi_balance, TREECOMPFUNC TreeComp, TREEDELFUNC TreeDeleteNode)
{
  TREENODE *p1;
  TREENODE *p2;
  int cmp;

  /* are we grounded?  if so, add the node "here" and set the rebalance
   * flag, then exit.
   */
  if (!*ppr) {
    *ppr = (TREENODE *) NewNode();
    (*ppr)->tree_l = NULL;
    (*ppr)->tree_r = NULL;
    (*ppr)->tree_b = 0;
    (*ppr)->tree_p = p_data;
    *pi_balance = TRUE;
    return;
  }

  /* compare the data using routine passed by caller.
   */
  cmp = TreeComp(p_data, (*ppr)->tree_p);

  /* if LESS, prepare to move to the left.
   */
  if (cmp < 0) {
    Sprout(&(*ppr)->tree_l, p_data, pi_balance, TreeComp, TreeDeleteNode);
    if (*pi_balance) {  /* left branch has grown longer */
      switch ((*ppr)->tree_b) {
        case 1: /* right branch WAS longer; balance is ok now */
          (*ppr)->tree_b = 0;
          *pi_balance = FALSE;
          break;
        case 0: /* balance WAS okay; now left branch longer */
          (*ppr)->tree_b = -1;
          break;
        case -1:
          /* left branch was already too long. rebalnce */
          p1 = (*ppr)->tree_l;
          if (p1->tree_b == -1) { /* LL */
            (*ppr)->tree_l = p1->tree_r;
            p1->tree_r = *ppr;
            (*ppr)->tree_b = 0;
            *ppr = p1;
          }
          else {      /* double LR */
            p2 = p1->tree_r;
            p1->tree_r = p2->tree_l;
            p2->tree_l = p1;
          
            (*ppr)->tree_l = p2->tree_r;
            p2->tree_r = *ppr;
          
            if (p2->tree_b == -1)
              (*ppr)->tree_b = 1;
            else
              (*ppr)->tree_b = 0;
          
            if (p2->tree_b == 1)
              p1->tree_b = -1;
            else
              p1->tree_b = 0;
            *ppr = p2;
          }
        (*ppr)->tree_b = 0;
        *pi_balance = FALSE;
        break;
      } 
    }
    return;
  }

  /* if MORE, prepare to move to the right.
   */
  if (cmp > 0) {
    Sprout(&(*ppr)->tree_r, p_data, pi_balance,
      TreeComp, TreeDeleteNode);
    if (*pi_balance) {  /* right branch has grown longer */
      switch ((*ppr)->tree_b) {
        case -1:
          (*ppr)->tree_b = 0;
          *pi_balance = FALSE;
          break;
        case 0:
          (*ppr)->tree_b = 1;
          break;
        case 1:
          p1 = (*ppr)->tree_r;
          if (p1->tree_b == 1) {  /* RR */
            (*ppr)->tree_r = p1->tree_l;
            p1->tree_l = *ppr;
            (*ppr)->tree_b = 0;
            *ppr = p1;
          }
          else {      /* double RL */
            p2 = p1->tree_l;
            p1->tree_l = p2->tree_r;
            p2->tree_r = p1;

            (*ppr)->tree_r = p2->tree_l;
            p2->tree_l = *ppr;

            if (p2->tree_b == 1)
              (*ppr)->tree_b = -1;
            else
              (*ppr)->tree_b = 0;

            if (p2->tree_b == -1)
              p1->tree_b = 1;
            else
              p1->tree_b = 0;

            *ppr = p2;
          }
        (*ppr)->tree_b = 0;
        *pi_balance = FALSE;
        break;
      } 
    }
    return;
  }

  /* not less, not more: this is the same key!  replace...
   */
  *pi_balance = FALSE;
  if (TreeDeleteNode)
    TreeDeleteNode((*ppr)->tree_p);
  (*ppr)->tree_p = p_data;
}

static void BalanceLeft(TREENODE **ppr_p, int *pi_balance)
{
  TREENODE  *p1, *p2;
  int b1, b2;

  switch ((*ppr_p)->tree_b) {
  case -1:
    (*ppr_p)->tree_b = 0;
    break;
  case 0:
    (*ppr_p)->tree_b = 1;
    *pi_balance = FALSE;
    break;
  case 1:
    p1 = (*ppr_p)->tree_r;
    b1 = p1->tree_b;
    if (b1 >= 0) {
      (*ppr_p)->tree_r = p1->tree_l;
      p1->tree_l = *ppr_p;
      if (b1 == 0) {
        (*ppr_p)->tree_b = 1;
        p1->tree_b = -1;
        *pi_balance = FALSE;
      } 
      else {
        (*ppr_p)->tree_b = 0;
        p1->tree_b = 0;
      }
      *ppr_p = p1;
    } else {
      p2 = p1->tree_l;
      b2 = p2->tree_b;
      p1->tree_l = p2->tree_r;
      p2->tree_r = p1;
      (*ppr_p)->tree_r = p2->tree_l;
      p2->tree_l = *ppr_p;
      if (b2 == 1)
        (*ppr_p)->tree_b = -1;
      else
        (*ppr_p)->tree_b = 0;
      if (b2 == -1)
        p1->tree_b = 1;
      else
        p1->tree_b = 0;
      *ppr_p = p2;
      p2->tree_b = 0;
    }
  }
}

static void BalanceRight(TREENODE **ppr_p, int *pi_balance)
{
  TREENODE  *p1, *p2;
  int b1, b2;

  switch ((*ppr_p)->tree_b) {
  case 1: 
    (*ppr_p)->tree_b = 0;
    break;
  case 0:
    (*ppr_p)->tree_b = -1;
    *pi_balance = FALSE;
    break;
  case -1:
    p1 = (*ppr_p)->tree_l;
    b1 = p1->tree_b;
    if (b1 <= 0) {
      (*ppr_p)->tree_l = p1->tree_r;
      p1->tree_r = *ppr_p;
      if (b1 == 0) {
        (*ppr_p)->tree_b = -1;
        p1->tree_b = 1;
        *pi_balance = FALSE;
      } else {
        (*ppr_p)->tree_b = 0;
        p1->tree_b = 0;
      }
      *ppr_p = p1;
    } else {
      p2 = p1->tree_r;
      b2 = p2->tree_b;
      p1->tree_r = p2->tree_l;
      p2->tree_l = p1;
      (*ppr_p)->tree_l = p2->tree_r;
      p2->tree_r = *ppr_p;
      if (b2 == -1)
        (*ppr_p)->tree_b = 1;
      else
        (*ppr_p)->tree_b = 0;
      if (b2 == 1)
        p1->tree_b = -1;
      else
        p1->tree_b = 0;
      *ppr_p = p2;
      p2->tree_b = 0;
    }
  }
}

static void Delete3(TREENODE  **ppr_r, int  *pi_balance, TREENODE **ppr_q, TREEDELFUNC TreeDeleteNode, int *pi_uar_called)
{
  if ((*ppr_r)->tree_r != NULL) {
    Delete3(&(*ppr_r)->tree_r, pi_balance, ppr_q,
        TreeDeleteNode, pi_uar_called);
    if (*pi_balance)
      BalanceRight(ppr_r, pi_balance);
  } else {
    if (TreeDeleteNode)
      TreeDeleteNode((*ppr_q)->tree_p);
    *pi_uar_called = TRUE;
    (*ppr_q)->tree_p = (*ppr_r)->tree_p;
    *ppr_q = *ppr_r;
    *ppr_r = (*ppr_r)->tree_l;
    *pi_balance = TRUE;
  }
}

static int Delete2(TREENODE **ppr_p, TREECOMPFUNC TreeComp, void *p_user,
                  TREEDELFUNC TreeDeleteNode, int *pi_balance, int *pi_uar_called)
{
  TREENODE  *pr_q;
  int i_comp, i_ret;

  if (*ppr_p == NULL) {
    return FALSE;
  }

  i_comp = TreeComp((*ppr_p)->tree_p, p_user);
  if (i_comp > 0) {
    i_ret = Delete2(&(*ppr_p)->tree_l, TreeComp, p_user, TreeDeleteNode,
            pi_balance, pi_uar_called);
    if (*pi_balance)
      BalanceLeft(ppr_p, pi_balance);
  }
  else if (i_comp < 0) {
    i_ret = Delete2(&(*ppr_p)->tree_r, TreeComp, p_user, TreeDeleteNode,
            pi_balance, pi_uar_called);
    if (*pi_balance)
      BalanceRight(ppr_p, pi_balance);
  }
  else {
    pr_q = *ppr_p;
    if (pr_q->tree_r == NULL) {
      *ppr_p = pr_q->tree_l;
      *pi_balance = TRUE;
    }
    else if (pr_q->tree_l == NULL) {
      *ppr_p = pr_q->tree_r;
      *pi_balance = TRUE;
    }
    else {
      Delete3(&pr_q->tree_l, pi_balance, &pr_q, TreeDeleteNode,
                pi_uar_called);
      if (*pi_balance)
        BalanceLeft(ppr_p, pi_balance);
    }
    if (!*pi_uar_called && TreeDeleteNode)
      TreeDeleteNode(pr_q->tree_p);
    FreeMem(pr_q); /* thanks to wuth@castrov.cuc.ab.ca */
    i_ret = TRUE;
  }
  return i_ret;
}


void TreeInit(TREENODE **ppr_tree)
{
  *ppr_tree = NULL;
  if (!TreeHeap) {
    TreeHeap = HeapCreate(0, 65536 * sizeof(TREENODE), 0);
  }
  if (!TreeArray)
    TreeArray = GetMem(65536 * sizeof(TREENODE));
  NextNode = 0;
}

void *TreeSearch(TREENODE **ppr_tree, TREECOMPFUNC TreeComp, void *p_user)
{
  int i_comp;

  if (*ppr_tree) {
    i_comp = TreeComp(p_user, (**ppr_tree).tree_p);
    if (i_comp > 0)
      return TreeSearch(&(**ppr_tree).tree_r, TreeComp, p_user);
    if (i_comp < 0)
      return TreeSearch(&(**ppr_tree).tree_l, TreeComp, p_user);

    /* not higher, not lower... this must be the one.
     */
    return (**ppr_tree).tree_p;
  }

  /* grounded. NOT found.
   */
  return NULL;
}

void TreeAdd(TREENODE **ppr_tree, TREECOMPFUNC TreeComp, void *p_user, TREEDELFUNC TreeDeleteNode)
{
  int i_balance = FALSE;

  Sprout(ppr_tree, p_user, &i_balance, TreeComp, TreeDeleteNode);
}

int TreeDelete(TREENODE  **ppr_p, TREECOMPFUNC TreeComp, void *p_user, TREEDELFUNC TreeDeleteNode)
{
  int i_balance = FALSE;
  int i_uar_called = FALSE;

  return Delete2(ppr_p, TreeComp, p_user, TreeDeleteNode, &i_balance, &i_uar_called);
}

int TreeTraverse(TREENODE **ppr_tree, TREETRAVFUNC TreeTraverseNode)
{
  if (!*ppr_tree)
    return TRUE;

  if (!TreeTraverse(&(**ppr_tree).tree_l, TreeTraverseNode))
    return FALSE;
  if (!TreeTraverseNode((**ppr_tree).tree_p))
    return FALSE;
  if (!TreeTraverse(&(**ppr_tree).tree_r, TreeTraverseNode))
    return FALSE;
  return TRUE;
}

void TreeDestroy(TREENODE **ppr_tree, TREEDELFUNC TreeDeleteNode)
{
  /*if (*ppr_tree) {
    TreeDestroy(&(**ppr_tree).tree_l, TreeDeleteNode);
    TreeDestroy(&(**ppr_tree).tree_r, TreeDeleteNode);
    if (TreeDeleteNode)
      TreeDeleteNode((**ppr_tree).tree_p);
    FreeMem(*ppr_tree);
    *ppr_tree = NULL;
  }*/
  NextNode = 0;
}
