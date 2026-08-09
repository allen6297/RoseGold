module loops

# break, continue, and list indexing (a[i])
fn main():
    var nums = [10, 20, 30, 40, 50]
    var total = 0
    var i = 0
    while i < len(nums):
        if nums[i] == 30:
            i = i + 1
            continue                # skip 30
        if nums[i] > 40:
            break                   # stop before 50
        total = total + nums[i]
        i = i + 1
    print("total =", total)         # 10 + 20 + 40 = 70

    nums[0] = 99                     # index assignment
    print("nums[0] =", nums[0])
