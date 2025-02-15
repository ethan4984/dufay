#ifndef FAYT_BST_H_
#define FAYT_BST_H_

#define BST_GENERIC_INSERT(TABLE_ROOT, BASE, NODE) ({ \
	__label__ out_bgi; \
	int ret = 0; \
	__typeof__(TABLE_ROOT) root = TABLE_ROOT; \
	__typeof__(TABLE_ROOT) parent = NULL; \
	if((NODE) == NULL) { ret = -1; goto out_bgi; } \
	for(; root;) { \
		parent = root; \
		if(root->BASE > (NODE)->BASE) root = root->left; \
		else root = root->right; \
	} \
	(NODE)->parent = parent; \
	if(parent == NULL) TABLE_ROOT = (NODE); \
	else if(parent->BASE > (NODE)->BASE) parent->left = (NODE); \
	else parent->right = (NODE); \
out_bgi: \
	(NODE)->left = NULL; \
	(NODE)->right = NULL; \
	ret; \
})

#define BST_GENERIC_DELETE(TABLE_ROOT, BASE, NODE) ({ \
	__label__ out_bgd; \
	int ret = 0; \
	__typeof__(NODE) parent = (NODE)->parent; \
	if((NODE) == NULL) { \
		ret = -1; \
		goto out_bgd; \
	} \
	if((NODE)->left == NULL && (NODE)->right == NULL) { \
		if(parent == NULL) TABLE_ROOT = NULL; \
		else if(parent->left == (NODE)) parent->left = NULL; \
		else parent->right = NULL; \
	} else if((NODE)->left && (NODE)->right == NULL) { \
		if(parent == NULL) TABLE_ROOT = (NODE)->left; \
		else if(parent->left == (NODE)) { \
			parent->left = (NODE)->left; \
			parent->left->parent = parent; \
		} else { \
			parent->right = (NODE)->left; \
			parent->right->parent = parent; \
		} \
		(NODE)->left->parent = parent; \
	} else if((NODE)->right && (NODE)->left == NULL) { \
		if(parent == NULL) TABLE_ROOT = (NODE)->right; \
		else if(parent->left == (NODE)) { \
			parent->left = (NODE)->right; \
			parent->left->parent = parent; \
		} else { \
			parent->right = (NODE)->right; \
			parent->right->parent = parent; \
		} \
		(NODE)->right->parent = parent; \
	} else { \
		__typeof__(NODE) successor = (NODE)->right; \
		for(; successor->left;) successor = successor->left; \
		if(successor->parent != (NODE)) { \
			successor->parent->left = successor->right; \
			if(successor->right) successor->right->parent = successor->parent; \
		} else { \
			successor->parent->right = successor->right; \
			if(successor->right) successor->right->parent = successor->parent; \
		} \
		if(parent == NULL) TABLE_ROOT = successor; \
		else if(parent->left == (NODE)) parent->left = successor; \
		else parent->right = successor; \
		successor->left = (NODE)->left; \
		if(successor->left) successor->left->parent = successor; \
		successor->right = (NODE)->right; \
		if(successor->right) successor->right->parent = successor; \
		successor->parent = parent; \
	} \
out_bgd: \
	ret; \
})

#endif
