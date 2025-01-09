import asyncio

async def hello(name):
    print(f"Hello, {name}!")
    await asyncio.sleep(1)  # Simulate IO-bound task
    print(f"Goodbye, {name}!")

async def main():
    print('start')
    await asyncio.gather(
        hello("Alice"),
        hello("Bob")
    )
    await hello('Tony')
    print('done')

asyncio.run(main())