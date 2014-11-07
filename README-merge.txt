
Merging local and remote entries, in case of conflict:

Eg:
remote: A---B---C---D
local:  A---E---F

Merging (via 'smit pull') shall proceed with the following steps:

Step 1:
- mark E as "merge-pending" (also on disk storage)
    => subsequent loading of the database shall detect this and refuse to do commands serve, ui, ... until the merge is resolved
    => children of E are automatically considered "merge-pending"

Step 2:
- download B, C, D

Step 3: merge E and B, C, D
- properties changed in E only are kept unchanged
- message of E is submitted to the user for removing or keeping
- properties changed in E that bring no modification to the issue are silently
  removed (for instance because B, C or D also contained the same property
  change)
- properties changed in E and also in one of B, C, D in a conflicting manner must be resolved by the user (interactively)
- E' is added as child of D
        => date
        => remaining properties changes of E
        => message, if kept
        => reference to E (merge-origin)
    (smit knows if E has already been merged thanks to this 'merge-origin')



Step 4: merge F and B, C, D, E'
(same algo as in step 3)

Step 5: when all merges are done:
- remove F
- remove E (this last removing also removes the global merge-pending state on
  the issue)


